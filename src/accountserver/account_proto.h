#ifndef SRC_ACCOUNTSERVER_ACCOUNT_PROTO_H
#define SRC_ACCOUNTSERVER_ACCOUNT_PROTO_H

/*
 * What the account server and its clients agree on: the hiscore categories,
 * the rows and the page views it answers with, and the line a world's record
 * travels as.
 *
 * The server (account_server.h) speaks HTTP on localhost, and every body it
 * answers with is lines of tab-separated fields, the first field naming the
 * line: readable with curl, parsed with a split. A text field (a name, a
 * quest's name) never holds a tab or a newline -- the writer turns any into a
 * space -- and comes last on its line when it may hold anything else.
 */

#include "torirsserver/torirs_server_accountlog.h"

#include <stddef.h>

/* The hiscore categories past the stats (0..24): */
/** Overall: total level, then total experience, over the listed skills. */
#define ACCOUNT_OVERALL 100
/** Quest points (varp 101). */
#define ACCOUNT_QUEST_POINTS 101
/* The skills the hiscores list and the overall counts: stat ids 0..23.
 * Summoning (24) is a feature-flagged port; its experience is kept, not
 * ranked overall. */
#define ACCOUNT_SKILLS 24
/* xp_day's stat for the total experience of the listed skills. */
#define ACCOUNT_TOTAL_XP 100

#define ACCOUNT_NAME_MAX 64
#define ACCOUNT_LABEL_MAX 80

/* A player page's history: the gain windows (a day, a week, a month), the
 * chart's reach in days, and how many sessions and log lines it shows. */
#define ACCOUNT_GAIN_WINDOWS 3
#define ACCOUNT_CHART_DAYS 365
#define ACCOUNT_SESSIONS_SHOWN 15
#define ACCOUNT_EVENTS_SHOWN 20

/* One record's line, newline included. */
#define ACCOUNT_RECORD_LINE_MAX 1024

struct AccountPlayer
{
    long long id;
    /** As the save is keyed: the page's URL. */
    char key[ACCOUNT_NAME_MAX];
    char name[ACCOUNT_NAME_MAX];
    int ranked;
    long long last_seen;
    /** Whole experience per stat. */
    long long xp[TORIRSSERVER_ACCOUNTLOG_STATS];
    int quest_points;
};

struct AccountRow
{
    char key[ACCOUNT_NAME_MAX];
    char name[ACCOUNT_NAME_MAX];
    /** The category's score: experience, quest points, or for OVERALL
     *  AccountProto_OverallScore's packing. */
    long long score;
};

struct AccountSession
{
    long long start;
    long long end;
    long long xp_gained;
    /** -1 when nothing gained. */
    int top_stat;
    long long top_gain;
};

struct AccountEvent
{
    long long time;
    enum ToriRSServerAdventureKind kind;
    int amount;
    char label[ACCOUNT_LABEL_MAX];
};

/*
 * Everything a player's page shows, in one answer (GET /view/player).
 *
 *   player  <id> <ranked> <last_seen> <quest_points> <key> <name>
 *   xp      <25 stats' whole experience>
 *   rank    <overall> <24 skills>          0 = not ranked there
 *   event   <time> <kind> <amount> <label>               newest first
 *
 * and with history=1 (the owner's view):
 *
 *   gain    <window> <partial> <24 skills' base> <total's base>
 *   chart   <day> <total experience>                      oldest first
 *   session <start> <end> <xp_gained> <top_stat> <top_gain> newest first
 *   sessions <count> <played seconds>
 *
 * A gain's base is the experience at the end of the day before the window;
 * `partial` 1 when the history starts later, and the base is then the first
 * recorded day's.
 */
struct AccountPlayerView
{
    struct AccountPlayer player;
    int rank_overall;
    int rank[ACCOUNT_SKILLS];
    struct AccountEvent events[ACCOUNT_EVENTS_SHOWN];
    int event_count;

    int has_history;
    int gain_partial[ACCOUNT_GAIN_WINDOWS];
    /** [window][skill], the total at [window][ACCOUNT_SKILLS]. */
    long long gain_base[ACCOUNT_GAIN_WINDOWS][ACCOUNT_SKILLS + 1];
    long long chart_day[ACCOUNT_CHART_DAYS];
    long long chart_xp[ACCOUNT_CHART_DAYS];
    int chart_count;
    struct AccountSession sessions[ACCOUNT_SESSIONS_SHOWN];
    int session_count;
    int session_total;
    long long played;
};

/** The days each gain window looks back. */
extern int const g_account_gain_window_days[ACCOUNT_GAIN_WINDOWS];

/** OldSchool's level for whole experience. */
int
AccountProto_LevelForXp(long long xp);

/** OVERALL's score: total level above total experience, so one descending
 *  order is level first and experience second. */
long long
AccountProto_OverallScore(
    int total_level,
    long long total_xp);

void
AccountProto_OverallSplit(
    long long score,
    int* out_total_level,
    long long* out_total_xp);

/**
 * A world's record as one line:
 *   <seq> <kind> <time> <admin> <quest_points> <session_start> <event>
 *   <amount> <25 xp_tenths> <25 session_xp_tenths> <name> <label>
 * Returns its length, newline included.
 */
int
AccountProto_RecordFormat(
    struct ToriRSServerAccountRecord const* record,
    long long seq,
    char out[ACCOUNT_RECORD_LINE_MAX]);

/** A line (without its newline; modified in place) back into a record. 0
 *  when it is not one: a torn line, or not this format. */
int
AccountProto_RecordParse(
    char* line,
    struct ToriRSServerAccountRecord* out,
    long long* out_seq);

/** `line`'s tab-separated fields, in place: their starts into `fields`, at
 *  most `max`. Returns how many. */
int
AccountProto_Fields(
    char* line,
    char** fields,
    int max);

/** `text` with any tab, CR or LF made a space, into `out`. */
void
AccountProto_Clean(
    char const* text,
    char* out,
    size_t cap);

#endif
