/*
 * The account server's shared vocabulary. See account_proto.h.
 */
#include "account_proto.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fields before the two stat tables, then the tables, then name and label. */
#define RECORD_HEAD_FIELDS 8
#define RECORD_FIELDS (RECORD_HEAD_FIELDS + 2 * TORIRSSERVER_ACCOUNTLOG_STATS + 3)

int const g_account_gain_window_days[ACCOUNT_GAIN_WINDOWS] = { 1, 7, 30 };

int
AccountProto_LevelForXp(long long xp)
{
    static long long level_xp[100];
    int level = 1;

    if( !level_xp[2] )
    {
        double points = 0;

        for( int at = 1; at < 99; at++ )
        {
            points += floor(at + 300.0 * pow(2.0, at / 7.0));
            level_xp[at + 1] = (long long)floor(points / 4);
        }
    }
    while( level < 99 && xp >= level_xp[level + 1] )
        level++;
    return level;
}

/* Total experience fits 33 bits (24 skills at 200M each, under 2^33), so the
 * level goes above it. */
long long
AccountProto_OverallScore(
    int total_level,
    long long total_xp)
{
    assert(total_xp >= 0);
    assert(total_xp < (1LL << 33));
    return ((long long)total_level << 33) | total_xp;
}

void
AccountProto_OverallSplit(
    long long score,
    int* out_total_level,
    long long* out_total_xp)
{
    assert(out_total_level);
    assert(out_total_xp);
    *out_total_level = (int)(score >> 33);
    *out_total_xp = score & ((1LL << 33) - 1);
}

void
AccountProto_Clean(
    char const* text,
    char* out,
    size_t cap)
{
    size_t i = 0;

    assert(text);
    assert(out);
    assert(cap > 0);
    for( ; text[i] && i + 1 < cap; i++ )
        out[i] = text[i] == '\t' || text[i] == '\n' || text[i] == '\r' ? ' ' : text[i];
    out[i] = '\0';
}

int
AccountProto_Fields(
    char* line,
    char** fields,
    int max)
{
    int count = 0;

    assert(line);
    assert(fields);
    while( count < max )
    {
        char* tab = strchr(line, '\t');

        fields[count++] = line;
        if( !tab )
            break;
        *tab = '\0';
        line = tab + 1;
    }
    return count;
}

int
AccountProto_RecordFormat(
    struct ToriRSServerAccountRecord const* r,
    long long seq,
    char out[ACCOUNT_RECORD_LINE_MAX])
{
    char name[TORIRSSERVER_ACCOUNTLOG_NAME_MAX];
    char display[TORIRSSERVER_ACCOUNTLOG_NAME_MAX];
    char label[TORIRSSERVER_ACCOUNTLOG_LABEL_MAX];
    int len;

    assert(r);
    assert(out);
    AccountProto_Clean(r->name, name, sizeof(name));
    AccountProto_Clean(r->display, display, sizeof(display));
    AccountProto_Clean(r->label, label, sizeof(label));
    len = snprintf(out, ACCOUNT_RECORD_LINE_MAX, "%lld\t%d\t%lld\t%d\t%d\t%lld\t%d\t%d", seq,
                   (int)r->kind, r->time, r->admin, r->quest_points, r->session_start,
                   (int)r->event, r->amount);
    for( int i = 0; i < TORIRSSERVER_ACCOUNTLOG_STATS; i++ )
        len += snprintf(out + len, (size_t)(ACCOUNT_RECORD_LINE_MAX - len), "\t%d", r->xp_tenths[i]);
    for( int i = 0; i < TORIRSSERVER_ACCOUNTLOG_STATS; i++ )
        len += snprintf(out + len, (size_t)(ACCOUNT_RECORD_LINE_MAX - len), "\t%d",
                        r->session_xp_tenths[i]);
    len += snprintf(out + len, (size_t)(ACCOUNT_RECORD_LINE_MAX - len), "\t%s\t%s\t%s\n", name,
                    display, label);
    /* 58 numbers and three short texts: far inside the line. */
    assert(len < ACCOUNT_RECORD_LINE_MAX);
    return len;
}

int
AccountProto_RecordParse(
    char* line,
    struct ToriRSServerAccountRecord* out,
    long long* out_seq)
{
    char* f[RECORD_FIELDS + 1];
    int count;
    int at = RECORD_HEAD_FIELDS;

    assert(line);
    assert(out);
    assert(out_seq);
    memset(out, 0, sizeof(*out));
    /* One spare slot: a line with more fields than a record is not one. */
    count = AccountProto_Fields(line, f, RECORD_FIELDS + 1);
    if( count != RECORD_FIELDS )
        return 0;
    for( int i = 0; i < RECORD_FIELDS - 3; i++ )
    {
        char* end;

        strtoll(f[i], &end, 10);
        if( end == f[i] || *end != '\0' )
            return 0;
    }
    *out_seq = strtoll(f[0], NULL, 10);
    out->kind = (enum ToriRSServerAccountLogKind)atoi(f[1]);
    out->time = strtoll(f[2], NULL, 10);
    out->admin = atoi(f[3]);
    out->quest_points = atoi(f[4]);
    out->session_start = strtoll(f[5], NULL, 10);
    out->event = (enum ToriRSServerAdventureKind)atoi(f[6]);
    out->amount = atoi(f[7]);
    for( int i = 0; i < TORIRSSERVER_ACCOUNTLOG_STATS; i++ )
        out->xp_tenths[i] = atoi(f[at++]);
    for( int i = 0; i < TORIRSSERVER_ACCOUNTLOG_STATS; i++ )
        out->session_xp_tenths[i] = atoi(f[at++]);
    snprintf(out->name, sizeof(out->name), "%s", f[at++]);
    snprintf(out->display, sizeof(out->display), "%s", f[at++]);
    snprintf(out->label, sizeof(out->label), "%s", f[at]);
    if( *out_seq <= 0 || !out->name[0] || out->kind < TORIRSSERVER_ACCOUNTLOG_LOGIN ||
        out->kind > TORIRSSERVER_ACCOUNTLOG_EVENT )
        return 0;
    return 1;
}
