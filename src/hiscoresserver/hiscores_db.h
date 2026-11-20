#ifndef SRC_HISCORESSERVER_HISCORES_DB_H
#define SRC_HISCORESSERVER_HISCORES_DB_H

/*
 * The hiscores database: `<saves>/hiscores.sqlite`, opened by the hiscores
 * server (hiscores_server.h) and by nothing else.
 *
 *   player(id, key, name, ranked, last_seen)      a character, as its save is
 *                                                 keyed; ranked 0 = staff
 *   score(player, category, score, since, ranked) the hiscores
 *   xp_day(player, stat, day, xp)                 a stat's experience at the
 *                                                 end of each day it changed
 *   session(player, start, end, ...)              one row per logout
 *   event(id, player, time, kind, amount, label)  the adventure log
 *   source(name, seq)                             each world's last record
 *                                                 applied (schema 2)
 *
 * **Who writes what.** Each world sends its records to the hiscores server
 * (torirs_server_hiscoreslink.h), which applies them in order and once each --
 * a record at or below its world's `source.seq` is a resend and is skipped;
 * the website and `torirsserver --account admin` set a player's ranking. The
 * website reads the pages from it.
 *
 * **Hiscores are kept, not computed.** A `score` row per player per category,
 * and one index of the ranked rows ordered as the hiscores are: a page is a
 * range of that index, a rank is a count along it. A tie goes to whoever
 * reached the score first (`since`), as OldSchool's does. Staff (`admin = 1`
 * in the player's account record) keep their rows -- their own page shows
 * them -- but with ranked 0, which the index leaves out, so they appear on no
 * list and push nobody down. The flag comes with every login record and with
 * `--account admin`.
 *
 * **History is per day.** `xp_day` holds a stat's experience at the end of
 * each day it changed; experience at any moment is the newest row at or
 * before it, so a gain over a day, a week or a month is one lookup per stat,
 * and the chart is one range read of the total.
 *
 * **Made once, from what it replaced.** The first open creates the schema
 * and copies in what `<saves>/accounts.sqlite` held -- the database this was
 * split out of, which also held the website's logins (website_users_db.h) --
 * or, with no such file, what the files before it held: every character
 * save's stats, and each `history/<name>.log`. Neither is read again.
 *
 * Every function opens the database on first use and aborts when it cannot:
 * a world whose hiscores silently stopped would look like nobody played.
 */

#include "hiscores_proto.h"

#include <stddef.h>

/** `<saves>/hiscores.sqlite`. Static storage. */
const char*
HiscoresDb_Path(void);

/** Open the database now rather than at first use, importing the files it
 *  replaced if it is new: the server's boot, so a problem shows there. */
void
HiscoresDb_Open(void);

/** Close this process's connection; the next call opens it again (a test
 *  that moves TORIRSSERVER_SAVES). */
void
HiscoresDb_Close(void);

/* ------------------------------------------------------------------ */
/* What the worlds report                                              */
/* ------------------------------------------------------------------ */

/**
 * Apply `count` records from the world `source`, in order, as one
 * transaction; `seqs[i]` is records[i]'s sequence number, rising. A record at
 * or below the source's last applied number is a resend and is skipped.
 * Returns the source's last applied number afterwards: what to acknowledge.
 */
long long
HiscoresDb_ApplyBatch(
    const char* source,
    struct ToriRSServerHiscoresRecord const* records,
    long long const* seqs,
    int count);

/** Rank a player or not (staff are not); a name with no player yet takes it
 *  at its first login record, which carries the account's flag. */
void
HiscoresDb_SetRanked(
    const char* display_name,
    int ranked);

/* ------------------------------------------------------------------ */
/* What the site reads                                                 */
/* ------------------------------------------------------------------ */

/** The player saved under `key`. 0 when there is none. */
int
HiscoresDb_Player(
    const char* key,
    struct HiscoresPlayer* out);

/** Everything a player's page shows (hiscores_proto.h); `history` 1 adds the
 *  owner's part. 0 when there is no such player. */
int
HiscoresDb_PlayerView(
    const char* key,
    int history,
    struct HiscoresPlayerView* out);

/** Ranks `first`.. of `category`, at most `max`, best first. Returns how many. */
int
HiscoresDb_Hiscores(
    int category,
    int first,
    int max,
    struct HiscoresRow* out);

/** How many players `category` ranks. */
int
HiscoresDb_RankedCount(int category);

/** The player's rank in `category`, 1-based; 0 when not ranked there. */
int
HiscoresDb_Rank(
    long long player,
    int category);

/** `stat`'s experience (or HISCORES_TOTAL_XP) at the end of `day` (unix time
 *  / 86400): the newest row at or before it. -1 when the history starts
 *  later. */
long long
HiscoresDb_XpAt(
    long long player,
    int stat,
    long long day);

/** The first day the history has for `stat`; -1 when none. */
long long
HiscoresDb_XpFirstDay(
    long long player,
    int stat);

/** `stat`'s rows, oldest first, at most `max`: the newest `max` of them. */
int
HiscoresDb_XpDays(
    long long player,
    int stat,
    long long* out_day,
    long long* out_xp,
    int max);

/** The newest sessions, newest first, at most `max`; `*out_count` and
 *  `*out_played` cover every session recorded. */
int
HiscoresDb_Sessions(
    long long player,
    struct HiscoresSession* out,
    int max,
    int* out_count,
    long long* out_played);

/** The newest adventure log lines, newest first, at most `max`. */
int
HiscoresDb_Events(
    long long player,
    struct HiscoresEvent* out,
    int max);

#endif
