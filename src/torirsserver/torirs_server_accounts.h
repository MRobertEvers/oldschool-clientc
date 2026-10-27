#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_ACCOUNTS_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_ACCOUNTS_H

/*
 * The account database: `<saves>/accounts.sqlite`.
 *
 * Everything the account site and the sign-in gate keep about people, in one
 * file both processes open:
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
 *
 * **Who writes what.** io_server writes web logins and invites (an invite is
 * redeemed on its page) and torirsserver's `--account` writes them for an
 * operator. The game server writes the rest, from its world's account log
 * (torirs_server_accountlog.h) after each tick; io_server only reads it. WAL
 * mode, so the site's reads never wait on the game's writes or the reverse.
 *
 * **Hiscores are kept, not computed.** A `score` row per player per category,
 * and one index of the ranked rows ordered as the hiscores are: a page is a
 * range of that index, a rank is a count along it. A tie goes to whoever
 * reached the score first (`since`), as OldSchool's does. Staff (`admin = 1`
 * in the player's account record) keep their rows -- their own page shows
 * them -- but with ranked 0, which the index leaves out, so they appear on no
 * list and push nobody down. The flag is taken at every login and by
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

#include "torirs_server_accountlog.h"
#include "torirs_server_auth.h"

#include <stddef.h>

/* The hiscore categories past the stats (0..24): */
/** Overall: total level, then total experience, over the listed skills. */
#define TORIRSSERVER_ACCOUNTS_OVERALL 100
/** Quest points (varp 101). */
#define TORIRSSERVER_ACCOUNTS_QUEST_POINTS 101
/* The skills the hiscores list and the overall counts: stat ids 0..23.
 * Summoning (24) is a feature-flagged port; its experience is kept, not
 * ranked overall. */
#define TORIRSSERVER_ACCOUNTS_SKILLS 24
/* xp_day's stat for the total experience of the listed skills. */
#define TORIRSSERVER_ACCOUNTS_TOTAL_XP 100

#define TORIRSSERVER_ACCOUNTS_NAME_MAX 64
#define TORIRSSERVER_ACCOUNTS_LABEL_MAX 80

/** `<saves>/accounts.sqlite`. Static storage. */
const char*
ToriRSServer_AccountsPath(void);

/** Open the database now rather than at first use, importing the files it
 *  replaced if it is new: a server's boot, so a problem shows there. */
void
ToriRSServer_AccountsOpen(void);

/** Close this process's connection; the next call opens it again (a test
 *  that moves TORIRSSERVER_SAVES). */
void
ToriRSServer_AccountsClose(void);

/* ------------------------------------------------------------------ */
/* Web logins and invites                                              */
/* ------------------------------------------------------------------ */

/**
 * Check a web login for the sign-in gate. Never claims: a name with no login
 * is REFUSED, because every one came from an invite. Not a player account;
 * see torirs_server_auth.h.
 */
enum ToriRSServerAuthResult
ToriRSServer_GateLogin(
    const char* name,
    const char* password);

/** An operator making a web login without an invite (`torirsserver
 *  --account login`). Same rules as an invite's. */
enum ToriRSServerCreateResult
ToriRSServer_GateLoginCreate(
    const char* name,
    const char* password,
    const char** out_problem);

/** Delete a web login (`--account revoke`): it cannot sign in again. A ticket
 *  already issued lasts until it expires, as when the login was a file and
 *  revoking it was deleting the file. 1 when there was one. */
int
ToriRSServer_GateLoginRevoke(const char* name);

/** Every web login's name, oldest first, each handed to `each`. */
void
ToriRSServer_GateLoginEach(void (*each)(const char* name));

/**
 * Make an invite, redeemed at the gate's `/invite/<code>` for a web login.
 * `name` NULL lets the visitor choose the name; otherwise only that name can
 * redeem it (it must pass AuthNameProblem).
 */
void
ToriRSServer_InviteCreate(
    const char* name,
    char out_code[TORIRSSERVER_INVITE_CODE_BYTES * 2 + 1]);

/** 1 when `code` is an unredeemed invite; `out_name` gets the name it is for,
 *  "" when the visitor chooses. */
int
ToriRSServer_InviteLookup(
    const char* code,
    char* out_name,
    size_t cap);

/** Spend `code` on a new web login `name` / `password`. The login and the
 *  spending are one transaction, so an invite is never spent twice.
 *  `*out_problem` is a sentence for BAD_NAME and WEAK, else NULL. */
enum ToriRSServerInviteResult
ToriRSServer_InviteRedeem(
    const char* code,
    const char* name,
    const char* password,
    const char** out_problem);

/* ------------------------------------------------------------------ */
/* What the world reports                                              */
/* ------------------------------------------------------------------ */

/** Write and empty `log`, as one transaction. */
void
ToriRSServer_AccountsApply(struct ToriRSServerAccountLog* log);

/** Rank a player or not (staff are not); a name with no player yet takes it
 *  at its first login, which reads the account record. */
void
ToriRSServer_AccountsSetRanked(
    const char* display_name,
    int ranked);

/* ------------------------------------------------------------------ */
/* What the site reads                                                 */
/* ------------------------------------------------------------------ */

struct ToriRSServerAccountsPlayer
{
    long long id;
    /** As the save is keyed: the page's URL. */
    char key[TORIRSSERVER_ACCOUNTS_NAME_MAX];
    char name[TORIRSSERVER_ACCOUNTS_NAME_MAX];
    int ranked;
    long long last_seen;
    /** Whole experience per stat. */
    long long xp[TORIRSSERVER_ACCOUNTLOG_STATS];
    int quest_points;
};

/** The player saved under `key`. 0 when there is none. */
int
ToriRSServer_AccountsPlayer(
    const char* key,
    struct ToriRSServerAccountsPlayer* out);

struct ToriRSServerAccountsRow
{
    char key[TORIRSSERVER_ACCOUNTS_NAME_MAX];
    char name[TORIRSSERVER_ACCOUNTS_NAME_MAX];
    /** The category's score: experience, quest points, or for OVERALL
     *  ToriRSServer_AccountsOverallScore's packing. */
    long long score;
};

/** Ranks `first`.. of `category`, at most `max`, best first. Returns how many. */
int
ToriRSServer_AccountsHiscores(
    int category,
    int first,
    int max,
    struct ToriRSServerAccountsRow* out);

/** How many players `category` ranks. */
int
ToriRSServer_AccountsRankedCount(int category);

/** The player's rank in `category`, 1-based; 0 when not ranked there. */
int
ToriRSServer_AccountsRank(
    long long player,
    int category);

/** OVERALL's score: total level above total experience, so one descending
 *  order is level first and experience second. */
long long
ToriRSServer_AccountsOverallScore(
    int total_level,
    long long total_xp);

void
ToriRSServer_AccountsOverallSplit(
    long long score,
    int* out_total_level,
    long long* out_total_xp);

/** OldSchool's level for whole experience. */
int
ToriRSServer_AccountsLevelForXp(long long xp);

/** `stat`'s experience (or TOTAL_XP) at the end of `day` (unix time / 86400):
 *  the newest row at or before it. -1 when the history starts later. */
long long
ToriRSServer_AccountsXpAt(
    long long player,
    int stat,
    long long day);

/** The first day the history has for `stat`; -1 when none. */
long long
ToriRSServer_AccountsXpFirstDay(
    long long player,
    int stat);

/** `stat`'s rows, oldest first, at most `max`: the newest `max` of them. */
int
ToriRSServer_AccountsXpDays(
    long long player,
    int stat,
    long long* out_day,
    long long* out_xp,
    int max);

struct ToriRSServerAccountsSession
{
    long long start;
    long long end;
    long long xp_gained;
    /** -1 when nothing gained. */
    int top_stat;
    long long top_gain;
};

/** The newest sessions, newest first, at most `max`; `*out_count` and
 *  `*out_played` cover every session recorded. */
int
ToriRSServer_AccountsSessions(
    long long player,
    struct ToriRSServerAccountsSession* out,
    int max,
    int* out_count,
    long long* out_played);

struct ToriRSServerAccountsEvent
{
    long long time;
    enum ToriRSServerAdventureKind kind;
    int amount;
    char label[TORIRSSERVER_ACCOUNTS_LABEL_MAX];
};

/** The newest adventure log lines, newest first, at most `max`. */
int
ToriRSServer_AccountsEvents(
    long long player,
    struct ToriRSServerAccountsEvent* out,
    int max);

#endif
