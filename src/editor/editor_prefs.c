/*
 * editor_prefs.ini, the pure half. editor_prefs.h has the format and the
 * policy; task_editor_prefs_io.c carries it.
 */

#include "editor/editor_prefs.h"

#include "mem/torirs_mem.h"
#include "ui/torirs_chrome_scale_q.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
Editor_PrefsInit(struct Editor_Prefs* prefs)
{
    assert(prefs);
    memset(prefs, 0, sizeof(*prefs));
    prefs->chrome_scale_q = EDITOR_PREFS_SCALE_AUTO;
    prefs->text_large = 0;
}

const char*
Editor_PrefsPath(void)
{
    const char* configured = getenv("TORIRS_EDITOR_PREFS");

    if( !configured )
        return EDITOR_PREFS_DEFAULT_PATH;
    return *configured ? configured : NULL;
}

/** `text` [start, end) with the blanks at both ends cut off. */
static void
prefs_trim(
    const char** start,
    const char** end)
{
    while( *start < *end && (**start == ' ' || **start == '\t') )
        (*start)++;
    while( *end > *start && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r') )
        (*end)--;
}

static int
prefs_is(
    const char* start,
    const char* end,
    const char* word)
{
    size_t const n = strlen(word);
    return (size_t)(end - start) == n && memcmp(start, word, n) == 0;
}

/** One `key = value` of [view]: 1 when it was understood. */
static int
prefs_line(
    struct Editor_Prefs* prefs,
    const char* key,
    const char* key_end,
    const char* value,
    const char* value_end)
{
    char word[16];
    int length = (int)(value_end - value);

    if( length <= 0 || length >= (int)sizeof(word) )
        return prefs_is(key, key_end, "version");
    memcpy(word, value, (size_t)length);
    word[length] = '\0';
    if( prefs_is(key, key_end, "version") )
        return 1;
    if( prefs_is(key, key_end, "chrome_scale") )
    {
        int percent;
        int q;
        if( strcmp(word, "auto") == 0 )
        {
            prefs->chrome_scale_q = EDITOR_PREFS_SCALE_AUTO;
            return 1;
        }
        if( sscanf(word, "%d", &percent) != 1 )
            return 0;
        q = percent * TORIRS_CHROME_SCALEQ_ONE / 100;
        /* Only the View menu's steps: a hand-written 133 is not one. */
        if( ToriRSChromeScaleQ_StepIndex(q) < 0 || ToriRSChromeScaleQ_Percent(q) != percent )
            return 0;
        prefs->chrome_scale_q = q;
        return 1;
    }
    if( prefs_is(key, key_end, "text_size") )
    {
        if( strcmp(word, "normal") == 0 )
            prefs->text_large = 0;
        else if( strcmp(word, "large") == 0 )
            prefs->text_large = 1;
        else
            return 0;
        return 1;
    }
    /* An unknown key is a newer editor's: ignored, not malformed. */
    return 1;
}

int
Editor_PrefsDecode(
    struct Editor_Prefs* prefs,
    const char* text,
    int size)
{
    const char* at;
    const char* end;
    int in_view = 0;
    int skipped = 0;

    assert(prefs);
    if( size == 0 )
        return 0;
    assert(text);
    assert(size > 0);
    at = text;
    end = text + size;
    while( at < end )
    {
        const char* line = at;
        const char* line_end = memchr(at, '\n', (size_t)(end - at));
        const char* semi;
        const char* eq;

        if( !line_end )
            line_end = end;
        at = line_end < end ? line_end + 1 : end;
        semi = memchr(line, ';', (size_t)(line_end - line));
        if( semi )
            line_end = semi;
        prefs_trim(&line, &line_end);
        if( line == line_end || line[0] == '#' )
            continue;
        if( line[0] == '[' )
        {
            in_view = prefs_is(line, line_end, "[view]");
            continue;
        }
        if( !in_view )
            continue;
        eq = memchr(line, '=', (size_t)(line_end - line));
        if( !eq )
        {
            skipped++;
            continue;
        }
        {
            const char* key = line;
            const char* key_end = eq;
            const char* value = eq + 1;
            const char* value_end = line_end;
            prefs_trim(&key, &key_end);
            prefs_trim(&value, &value_end);
            if( !prefs_line(prefs, key, key_end, value, value_end) )
                skipped++;
        }
    }
    return skipped;
}

void
Editor_PrefsEncode(
    const struct Editor_Prefs* prefs,
    char** out_text,
    int* out_size)
{
    char scale[16];
    char* text;
    int length;
    size_t const capacity = 256;

    assert(prefs);
    assert(out_text);
    assert(out_size);
    if( prefs->chrome_scale_q == EDITOR_PREFS_SCALE_AUTO )
        snprintf(scale, sizeof(scale), "auto");
    else
    {
        assert(ToriRSChromeScaleQ_StepIndex(prefs->chrome_scale_q) >= 0);
        snprintf(scale, sizeof(scale), "%d", ToriRSChromeScaleQ_Percent(prefs->chrome_scale_q));
    }
    text = ToriMem_Alloc(ToriMem_Process(), capacity);
    assert(text);
    length = snprintf(text, capacity,
        "; The map editor's view on this device (editor_prefs.h). View > Chrome scale\n"
        "; and View > Text size write it; a window popped out of this one reads it.\n"
        "[view]\n"
        "version = %d\n"
        "chrome_scale = %s\n"
        "text_size = %s\n",
        EDITOR_PREFS_VERSION, scale, prefs->text_large ? "large" : "normal");
    assert(length > 0);
    assert((size_t)length < capacity);
    *out_text = text;
    *out_size = length;
}
