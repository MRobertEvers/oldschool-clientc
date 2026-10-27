/*
 * Session history. See torirs_server_history.h.
 */
#include "torirs_server_history.h"

#include "torirs_server_savefile.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The newest this much of a file is read: ~250 bytes a logout, so well past
 * TORIRSSERVER_HISTORY_MAX entries. */
#define HISTORY_TAIL_BYTES (256 * 1024)

const char*
ToriRSServer_HistoryPath(const char* display_name)
{
    static char path[1024];
    char name[64];

    assert(display_name);
    ToriRSServer_SaveFileName(display_name, name, sizeof(name));
    if( !name[0] )
    {
        path[0] = '\0';
        return path;
    }
    snprintf(path, sizeof(path), "%s/history/%s.log", ToriRSServer_SaveDir(), name);
    return path;
}

/* One line, written whole with a single fwrite so a concurrent reader sees it
 * complete or not at all. */
static void
history_append(
    const char* display_name,
    const char* line)
{
    char path[1024];
    FILE* file;
    size_t len = strlen(line);

    snprintf(path, sizeof(path), "%s", ToriRSServer_HistoryPath(display_name));
    if( !path[0] )
        return;
    ToriRSServer_SaveMkdirParent(path);
    file = fopen(path, "ab");
    if( !file )
    {
        fprintf(stderr, "torirsserver: history: cannot open %s: %s\n", path, strerror(errno));
        return;
    }
    if( fwrite(line, 1, len, file) != len )
        fprintf(stderr, "torirsserver: history: short write to %s\n", path);
    fclose(file);
}

void
ToriRSServer_HistoryLogin(
    const char* display_name,
    long long now)
{
    char line[64];

    assert(display_name);
    snprintf(line, sizeof(line), "%lld login\n", now);
    history_append(display_name, line);
}

void
ToriRSServer_HistoryLogout(
    const char* display_name,
    long long now,
    const int xp_tenths[TORIRSSERVER_HISTORY_STATS])
{
    char line[64 + TORIRSSERVER_HISTORY_STATS * 12];
    size_t used;

    assert(display_name);
    assert(xp_tenths);
    used = (size_t)snprintf(line, sizeof(line), "%lld logout", now);
    for( int i = 0; i < TORIRSSERVER_HISTORY_STATS; i++ )
        used += (size_t)snprintf(line + used, sizeof(line) - used, " %d", xp_tenths[i]);
    assert(used + 2 < sizeof(line));
    line[used++] = '\n';
    line[used] = '\0';
    history_append(display_name, line);
}

/* One line into `out`; 0 for a line that is not an entry (torn, foreign). */
static int
history_parse(
    char* line,
    struct ToriRSServerHistoryEntry* out)
{
    char* scan = line;
    char* end;

    memset(out, 0, sizeof(*out));
    out->time = strtoll(scan, &end, 10);
    if( end == scan || out->time <= 0 )
        return 0;
    scan = end;
    while( *scan == ' ' )
        scan++;
    if( strncmp(scan, "login", 5) == 0 && (scan[5] == '\0' || scan[5] == ' ') )
    {
        out->kind = TORIRSSERVER_HISTORY_LOGIN;
        return 1;
    }
    if( strncmp(scan, "logout", 6) != 0 )
        return 0;
    scan += 6;
    for( int i = 0; i < TORIRSSERVER_HISTORY_STATS; i++ )
    {
        out->xp_tenths[i] = strtoll(scan, &end, 10);
        /* A torn line lacks its tail: every stat or the line is not one. */
        if( end == scan )
            return 0;
        scan = end;
    }
    out->kind = TORIRSSERVER_HISTORY_LOGOUT;
    return 1;
}

void
ToriRSServer_HistoryRead(
    const char* display_name,
    struct ToriRSServerHistory* out)
{
    char path[1024];
    char* data;
    FILE* file;
    long size;
    long start;
    size_t got;
    char* line;
    int skip_first;
    size_t total = 0;

    assert(display_name);
    assert(out);
    out->count = 0;
    snprintf(path, sizeof(path), "%s", ToriRSServer_HistoryPath(display_name));
    if( !path[0] )
        return;
    file = fopen(path, "rb");
    if( !file )
        return;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    start = size > HISTORY_TAIL_BYTES ? size - HISTORY_TAIL_BYTES : 0;
    /* Starting mid-file lands mid-line; that first partial line is dropped. */
    skip_first = start > 0;
    fseek(file, start, SEEK_SET);
    data = malloc((size_t)(size - start) + 1);
    assert(data);
    got = fread(data, 1, (size_t)(size - start), file);
    fclose(file);
    data[got] = '\0';

    /* A ring: past the cap the newest overwrite the oldest, and one rotation
     * at the end puts them back in order. */
    line = data;
    while( line && *line )
    {
        char* next = strchr(line, '\n');

        if( next )
            *next++ = '\0';
        if( skip_first )
            skip_first = 0;
        else if( history_parse(line, &out->entries[total % TORIRSSERVER_HISTORY_MAX]) )
            total++;
        line = next;
    }
    free(data);

    if( total <= TORIRSSERVER_HISTORY_MAX )
    {
        out->count = (int)total;
        return;
    }
    {
        size_t oldest = total % TORIRSSERVER_HISTORY_MAX;
        struct ToriRSServerHistoryEntry* copy = malloc(sizeof(out->entries));

        assert(copy);
        memcpy(copy, out->entries + oldest,
               (TORIRSSERVER_HISTORY_MAX - oldest) * sizeof(out->entries[0]));
        memcpy(copy + (TORIRSSERVER_HISTORY_MAX - oldest), out->entries,
               oldest * sizeof(out->entries[0]));
        memcpy(out->entries, copy, sizeof(out->entries));
        free(copy);
        out->count = TORIRSSERVER_HISTORY_MAX;
    }
}
