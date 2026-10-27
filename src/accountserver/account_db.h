#ifndef SRC_ACCOUNTSERVER_ACCOUNT_DB_H
#define SRC_ACCOUNTSERVER_ACCOUNT_DB_H

/*
 * The account database: `<saves>/accounts.sqlite`, opened by the account
 * server (account_server.h) and by nothing else.
 *
 *   web_login(key, name, password, created_at)   the gate's logins
 *   invite(code, name, created_at)                unredeemed invites
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
 * **Who writes what.** Everything goes through the account server: io_server
 * asks it to check a web login or spend an invite, `torirsserver --account`
 * to make or revoke one, and each world sends it its records
 * (torirs_server_accountlink.h), which it applies in order and once each --
 * a record at or below its world's `source.seq` is a resend and is skipped.
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
 * **Made once, from the files it replaced.** The first open creates the
 * schema and imports what the files held: the gate's logins
 * (`<saves>/gate/<name>.ini`) and invites (`gate/invites/<code>.ini`), every
 * character save's stats, and each `history/<name>.log`. Those files are not
 * read again; an operator may delete them.
 *
 * Every function opens the database on first use and aborts when it cannot:
 * a gate whose logins silently failed to open would refuse everyone, and a
 * world whose hiscores silently stopped would look like nobody played.
 */

#include "account_proto.h"
#include "torirsserver/torirs_server_auth.h"

#include <stddef.h>

/** `<saves>/accounts.sqlite`. Static storage. */
const char*
AccountDb_Path(void);

/** Open the database now rather than at first use, importing the files it
 *  replaced if it is new: the server's boot, so a problem shows there. */
void
AccountDb_Open(void);

/** Close this process's connection; the next call opens it again (a test
 *  that moves TORIRSSERVER_SAVES). */
void
AccountDb_Close(void);

/* ------------------------------------------------------------------ */
/* Web logins and invites                                              */
/* ------------------------------------------------------------------ */

/**
 * Check a web login for the sign-in gate. Never claims: a name with no login
 * is REFUSED, because every one came from an invite. Not a player account;
 * see torirs_server_auth.h.
 */
enum ToriRSServerAuthResult
AccountDb_GateLogin(
    const char* name,
    const char* password);

/** An operator making a web login without an invite. Same rules as an
 *  invite's. */
enum ToriRSServerCreateResult
AccountDb_GateLoginCreate(
    const char* name,
    const char* password,
    const char** out_problem);

/** Delete a web login: it cannot sign in again. A ticket already issued
 *  lasts until it expires, as when the login was a file and revoking it was
 *  deleting the file. 1 when there was one. */
int
AccountDb_GateLoginRevoke(const char* name);

/** Every web login's name, oldest first, each handed to `each`. */
void
AccountDb_GateLoginEach(
    void (*each)(const char* name, void* ctx),
    void* ctx);

/**
 * Make an invite, redeemed at the gate's `/invite/<code>` for a web login.
 * `name` NULL lets the visitor choose the name; otherwise only that name can
 * redeem it (it must pass AuthNameProblem).
 */
void
AccountDb_InviteCreate(
    const char* name,
    char out_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1]);

/** 1 when `code` is an unredeemed invite; `out_name` gets the name it is for,
 *  "" when the visitor chooses. */
int
AccountDb_InviteLookup(
    const char* code,
    char* out_name,
    size_t cap);

/** Spend `code` on a new web login `name` / `password`. The login and the
 *  spending are one transaction, so an invite is never spent twice.
 *  `*out_problem` is a sentence for BAD_NAME and WEAK, else NULL. */
enum ToriRSServerInviteResult
AccountDb_InviteRedeem(
    const char* code,
    const char* name,
    const char* password,
    const char** out_problem);

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
AccountDb_ApplyBatch(
    const char* source,
    struct ToriRSServerAccountRecord const* records,
    long long const* seqs,
    int count);

/** Rank a player or not (staff are not); a name with no player yet takes it
 *  at its first login record, which carries the account's flag. */
void
AccountDb_SetRanked(
    const char* display_name,
    int ranked);

/* ------------------------------------------------------------------ */
/* What the site reads                                                 */
/* ------------------------------------------------------------------ */

/** The player saved under `key`. 0 when there is none. */
int
AccountDb_Player(
    const char* key,
    struct AccountPlayer* out);

/** Everything a player's page shows (account_proto.h); `history` 1 adds the
 *  owner's part. 0 when there is no such player. */
int
AccountDb_PlayerView(
    const char* key,
    int history,
    struct AccountPlayerView* out);

/** Ranks `first`.. of `category`, at most `max`, best first. Returns how many. */
int
AccountDb_Hiscores(
    int category,
    int first,
    int max,
    struct AccountRow* out);

/** How many players `category` ranks. */
int
AccountDb_RankedCount(int category);

/** The player's rank in `category`, 1-based; 0 when not ranked there. */
int
AccountDb_Rank(
    long long player,
    int category);

/** `stat`'s experience (or ACCOUNT_TOTAL_XP) at the end of `day` (unix time
 *  / 86400): the newest row at or before it. -1 when the history starts
 *  later. */
long long
AccountDb_XpAt(
    long long player,
    int stat,
    long long day);

/** The first day the history has for `stat`; -1 when none. */
long long
AccountDb_XpFirstDay(
    long long player,
    int stat);

/** `stat`'s rows, oldest first, at most `max`: the newest `max` of them. */
int
AccountDb_XpDays(
    long long player,
    int stat,
    long long* out_day,
    long long* out_xp,
    int max);

/** The newest sessions, newest first, at most `max`; `*out_count` and
 *  `*out_played` cover every session recorded. */
int
AccountDb_Sessions(
    long long player,
    struct AccountSession* out,
    int max,
    int* out_count,
    long long* out_played);

/** The newest adventure log lines, newest first, at most `max`. */
int
AccountDb_Events(
    long long player,
    struct AccountEvent* out,
    int max);

#endif
