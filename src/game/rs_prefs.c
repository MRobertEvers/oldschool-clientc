/*
 * Device-local settings, on disk. See rs_prefs.h.
 */

#include "game/rs_prefs.h"
#include <assert.h>

#include "3rd/ini/ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Bumped only when a key changes meaning, never when one is added: an unknown
 * key is skipped on load and a missing one keeps its default, so an old file
 * and a new client already understand each other.
 */
#define RS_PREFS_VERSION 1

#define RS_PREFS_DEFAULT_PATH "preferences.ini"

/* Resizable, matching both RS_CS2Host_Init and `class79`'s own constructor. */
#define RS_PREFS_DEFAULT_WINDOW_MODE 2

/* Indexed by enum RS_CS2OptionKind. There is no client_options section: a
 * CLIENTOPTION id resolves to one of these two tables (RS_CS2Host_ClientOptionKind). */
static char const* const kind_section[RS_CS2_OPTION_KIND_COUNT] = {
    "game_options",
    "device_options",
};

/* Section states that are not one of the option kinds. */
#define PREFS_SECTION_NONE (-2)
#define PREFS_SECTION_META (-1)

char const*
RS_Prefs_Path(void)
{
    char const* configured = getenv("TORIRS_PREFS");

    if( !configured )
        return RS_PREFS_DEFAULT_PATH;
    /* An empty override is "do not touch the filesystem", which is what the
     * headless harnesses want: they run from the repo root and must not leave a
     * file behind, and a test that changes a volume must not inherit the last
     * run's. */
    return *configured ? configured : NULL;
}

void
RS_Prefs_Defaults(struct RS_Prefs* prefs)
{
    assert(prefs);
    for( int kind = 0; kind < RS_CS2_OPTION_KIND_COUNT; kind++ )
        for( int id = 0; id < RS_CS2_OPTION_MAX; id++ )
            prefs->options[kind][id] = RS_CS2Host_OptionDefault(kind, id);
    prefs->default_window_mode = RS_PREFS_DEFAULT_WINDOW_MODE;
}

/* ------------------------------------------------------------------ */
/* Read                                                                */
/* ------------------------------------------------------------------ */

/* 3rd/ini hands back whatever was around the '=' — this file is meant to be
 * hand-editable, so `7 = 40` has to mean what it looks like. */
static void
prefs_trim(char* text)
{
    size_t len;
    char* start = text;

    while( *start && isspace((unsigned char)*start) )
        start++;
    len = strlen(start);
    while( len > 0 && isspace((unsigned char)start[len - 1]) )
        len--;
    memmove(text, start, len);
    text[len] = '\0';
}

int
RS_Prefs_Decode(
    struct RS_Prefs* prefs,
    void const* data,
    int size)
{
    struct INIReader reader;
    struct INIElement element;
    int section = PREFS_SECTION_NONE;
    int version = 0;

    assert(prefs);
    RS_Prefs_Defaults(prefs);
    if( !data || size <= 0 )
        return 0;

    ini_reader_init(&reader);
    while( ini_reader_next(&reader, (uint8_t*)(uintptr_t)data, (uint32_t)size, &element) ==
           TORI_INI_ERR_OK )
    {
        if( element.kind == INI_ELEMENT_SECTION )
        {
            prefs_trim(element._section.name);
            /* A section from a newer client is skipped whole, rather than
             * having its keys read under whatever section came before it. */
            section = strcmp(element._section.name, "preferences") == 0 ? PREFS_SECTION_META
                                                                       : PREFS_SECTION_NONE;
            for( int kind = 0; kind < RS_CS2_OPTION_KIND_COUNT; kind++ )
                if( strcmp(element._section.name, kind_section[kind]) == 0 )
                    section = kind;
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL )
            continue;
        prefs_trim(element._keyval.name);
        prefs_trim(element._keyval.value);

        if( section == PREFS_SECTION_META )
        {
            if( strcmp(element._keyval.name, "version") == 0 )
                version = atoi(element._keyval.value);
            else if( strcmp(element._keyval.name, "default_window_mode") == 0 )
            {
                int mode = atoi(element._keyval.value);

                /* The CS2 windowmode domain has exactly two members; anything
                 * else is a hand-edit and keeps the default. */
                if( mode == 1 || mode == 2 )
                    prefs->default_window_mode = mode;
            }
            continue;
        }
        if( section >= 0 )
        {
            int id = atoi(element._keyval.name);

            /* An id this build has no room for is skipped rather than clamped:
             * writing it to a neighbouring slot would be a setting the player
             * never chose. An id the reference does not keep on disk is skipped
             * too — the same rule the writer applies, so a hand-edited file
             * cannot give this client persistence the reference lacks. */
            if( id >= 0 && id < RS_CS2_OPTION_MAX &&
                RS_CS2Host_OptionPersists(section, id) )
                prefs->options[section][id] = atoi(element._keyval.value);
        }
    }

    if( version > RS_PREFS_VERSION )
        TORIRS_LOG("prefs: the settings file is version %d and this client writes %d — keys "
                "this client does not know were ignored\n",
                version, RS_PREFS_VERSION);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Write                                                               */
/* ------------------------------------------------------------------ */

int
RS_Prefs_Encode(
    struct RS_Prefs const* prefs,
    void** out_data,
    int* out_size)
{
    /*
     * Bounded by construction: a header, plus one `<id>=<value>` line per
     * option that is not at its default, over three tables of
     * RS_CS2_OPTION_MAX. A line cannot exceed "64=-2147483648\n", so the
     * arithmetic below cannot be outgrown by a wider option table — the buffer
     * follows the table's size rather than a number someone has to remember to
     * raise.
     */
    char* text;
    int cap = 512 + RS_CS2_OPTION_KIND_COUNT * (64 + RS_CS2_OPTION_MAX * 24);
    int len = 0;

    assert(prefs);
    assert(out_data);
    assert(out_size);
    *out_data = NULL;
    *out_size = 0;
    text = (char*)malloc((size_t)cap);
    assert(text);

    len += snprintf(
        text + len, (size_t)(cap - len),
        "; torirs client preferences. Written by src/game/rs_prefs.c; safe to\n"
        "; hand-edit. These are device settings — the audio panel's volumes and\n"
        "; the rest of the CS2 client/game/device options. Account state lives\n"
        "; in the server's player save, not here.\n"
        ";\n"
        "; An option at its default is left out, so a default that changes later\n"
        "; still reaches a file written before it.\n"
        "\n[preferences]\n"
        "version=%d\n",
        RS_PREFS_VERSION);

    if( prefs->default_window_mode != RS_PREFS_DEFAULT_WINDOW_MODE )
        len += snprintf(
            text + len, (size_t)(cap - len), "default_window_mode=%d\n",
            prefs->default_window_mode);

    for( int kind = 0; kind < RS_CS2_OPTION_KIND_COUNT; kind++ )
    {
        int wrote_section = 0;

        for( int id = 0; id < RS_CS2_OPTION_MAX; id++ )
        {
            if( !RS_CS2Host_OptionPersists(kind, id) )
                continue;
            if( prefs->options[kind][id] == RS_CS2Host_OptionDefault(kind, id) )
                continue;
            if( !wrote_section )
            {
                len += snprintf(
                    text + len, (size_t)(cap - len), "\n[%s]\n; <option id> = <value>\n",
                    kind_section[kind]);
                wrote_section = 1;
            }
            len += snprintf(
                text + len, (size_t)(cap - len), "%d=%d\n", id, prefs->options[kind][id]);
        }
    }

    if( len < 0 || len >= cap )
    {
        free(text);
        return 0;
    }
    *out_data = text;
    *out_size = len;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Host                                                                */
/* ------------------------------------------------------------------ */

void
RS_Prefs_ApplyToHost(
    struct RS_Prefs const* prefs,
    struct RS_CS2Host* host)
{
    assert(prefs);
    assert(host);
    for( int kind = 0; kind < RS_CS2_OPTION_KIND_COUNT; kind++ )
        for( int id = 0; id < RS_CS2_OPTION_MAX; id++ )
            RS_CS2Host_SetOption(host, kind, id, prefs->options[kind][id]);
    host->default_window_mode = prefs->default_window_mode;
}

int
RS_Prefs_CaptureFromHost(
    struct RS_Prefs* prefs,
    struct RS_CS2Host const* host)
{
    int changed = 0;

    assert(prefs);
    assert(host);
    for( int kind = 0; kind < RS_CS2_OPTION_KIND_COUNT; kind++ )
        for( int id = 0; id < RS_CS2_OPTION_MAX; id++ )
        {
            int value = RS_CS2Host_GetOption(host, kind, id);

            if( prefs->options[kind][id] == value )
                continue;
            prefs->options[kind][id] = value;
            changed = 1;
        }
    /* Only a script's choice, never the boot config's — see
     * RS_CS2Host.default_window_mode_from_script. */
    if( host->default_window_mode_from_script && host->default_window_mode > 0 &&
        prefs->default_window_mode != host->default_window_mode )
    {
        prefs->default_window_mode = host->default_window_mode;
        changed = 1;
    }
    return changed;
}
