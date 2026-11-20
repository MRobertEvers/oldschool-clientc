#ifndef SRC_HISCORESSERVER_HISCORES_PROTO_H
#define SRC_HISCORESSERVER_HISCORES_PROTO_H

/*
 * What the hiscores server and its clients agree on: the hiscore categories,
 * the rows and the page views it answers with, and the line a world's record
 * travels as.
 *
 * The server (hiscores_server.h) speaks HTTP on localhost, and every body it
 * answers with is lines of tab-separated fields, the first field naming the
 * line: readable with curl, parsed with a split. A text field (a name, a
 * quest's name) never holds a tab or a newline -- the writer turns any into a
 * space -- and comes last on its line when it may hold anything else.
 */

#include "torirsserver/torirs_server_hiscoreslog.h"

#include <stddef.h>

/* The hiscore categories past the stats (0..24): */
/** Overall: total level, then total experience, over the listed skills. */
#define HISCORES_OVERALL 100
/** Quest points (varp 101). */
#define HISCORES_QUEST_POINTS 101
/* The skills the hiscores list and the overall counts: stat ids 0..23.
 * Summoning (24) is a feature-flagged port; its experience is kept, not
 * ranked overall. */
#define HISCORES_SKILLS 24
/* xp_day's stat for the total experience of the listed skills. */
#define HISCORES_TOTAL_XP 100

#define HISCORES_NAME_MAX 64
#define HISCORES_LABEL_MAX 80

/* A player page's history: the gain windows (a day, a week, a month), the
 * chart's reach in days, and how many sessions and log lines it shows. */
#define HISCORES_GAIN_WINDOWS 3
#define HISCORES_CHART_DAYS 365
#define HISCORES_SESSIONS_SHOWN 15
#define HISCORES_EVENTS_SHOWN 20

/* One record's line, newline included. */
#define HISCORES_RECORD_LINE_MAX 1024

struct HiscoresPlayer
{
    long long id;
    /** As the save is keyed: the page's URL. */
    char key[HISCORES_NAME_MAX];
    char name[HISCORES_NAME_MAX];
    int ranked;
    long long last_seen;
    /** Whole experience per stat. */
    long long xp[TORIRSSERVER_HISCORESLOG_STATS];
    int quest_points;
};

struct HiscoresRow
{
    char key[HISCORES_NAME_MAX];
    char name[HISCORES_NAME_MAX];
    /** The category's score: experience, quest points, or for OVERALL
     *  HiscoresProto_OverallScore's packing. */
    long long score;
};

struct HiscoresSession
{
    long long start;
    long long end;
    long long xp_gained;
    /** -1 when nothing gained. */
    int top_stat;
    long long top_gain;
};

struct HiscoresEvent
{
    long long time;
    enum ToriRSServerAdventureKind kind;
    int amount;
    char label[HISCORES_LABEL_MAX];
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
struct HiscoresPlayerView
{
    struct HiscoresPlayer player;
    int rank_overall;
    int rank[HISCORES_SKILLS];
    struct HiscoresEvent events[HISCORES_EVENTS_SHOWN];
    int event_count;

    int has_history;
    int gain_partial[HISCORES_GAIN_WINDOWS];
    /** [window][skill], the total at [window][HISCORES_SKILLS]. */
    long long gain_base[HISCORES_GAIN_WINDOWS][HISCORES_SKILLS + 1];
    long long chart_day[HISCORES_CHART_DAYS];
    long long chart_xp[HISCORES_CHART_DAYS];
    int chart_count;
    struct HiscoresSession sessions[HISCORES_SESSIONS_SHOWN];
    int session_count;
    int session_total;
    long long played;
};

/** The days each gain window looks back. */
extern int const g_hiscores_gain_window_days[HISCORES_GAIN_WINDOWS];

/** OldSchool's level for whole experience. */
int
HiscoresProto_LevelForXp(long long xp);

/** OVERALL's score: total level above total experience, so one descending
 *  order is level first and experience second. */
long long
HiscoresProto_OverallScore(
    int total_level,
    long long total_xp);

void
HiscoresProto_OverallSplit(
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
HiscoresProto_RecordFormat(
    struct ToriRSServerHiscoresRecord const* record,
    long long seq,
    char out[HISCORES_RECORD_LINE_MAX]);

/** A line (without its newline; modified in place) back into a record. 0
 *  when it is not one: a torn line, or not this format. */
int
HiscoresProto_RecordParse(
    char* line,
    struct ToriRSServerHiscoresRecord* out,
    long long* out_seq);

/** `line`'s tab-separated fields, in place: their starts into `fields`, at
 *  most `max`. Returns how many. */
int
HiscoresProto_Fields(
    char* line,
    char** fields,
    int max);

/** `text` with any tab, CR or LF made a space, into `out`. */
void
HiscoresProto_Clean(
    char const* text,
    char* out,
    size_t cap);

#endif
