/*
 * torirsmapedctl's encounter verbs (torirs_mapedctl_encounters.h).
 */

#include "torirs_mapedctl_encounters.h"

#include "editor/editor_encounters.h"
#include "editor/editor_host.h"
#include "editor/editor_stamps.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Splices a verb sends at most: a document of a few hundred sets rewritten
 *  whole is a few dozen chunks. */
#define CTL_ENCOUNTERS_PLAN_MAX 256

/** DOC_OPEN the ENCOUNTERS document: its text (owned, NUL-terminated; ""
 *  when there is no file yet) and the parse. 0 (said why) when it cannot. */
static int
ctl_encounters_load(
    struct EditorHost* host,
    struct Editor_EncountersFile* out,
    char** out_text,
    size_t* out_length)
{
    struct EditorHost_DocText text;
    struct Editor_EncountersParseResult parsed;
    size_t length = 0;
    char* copy;

    Editor_EncountersInit(out);
    memset(&text, 0, sizeof(text));
    if( Editor_HostMapEdDocOpen(host, EDITOR_DOC_TEXT, EDITOR_ENCOUNTERS_DOC_KEY, &text) != EDITOR_HOST_OK )
    {
        fprintf(stderr, "torirsmapedctl: the server has no %s document\n", EDITOR_ENCOUNTERS_FILE_PATH);
        Editor_HostDocTextFree(&text);
        return 0;
    }
    if( text.part_count > 0 && text.parts[0].data )
        length = text.parts[0].size;
    copy = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(copy);
    if( length > 0 )
        memcpy(copy, text.parts[0].data, length);
    copy[length] = '\0';
    Editor_HostDocTextFree(&text);
    parsed = Editor_EncountersParse(out, copy, length);
    if( !parsed.ok )
    {
        fprintf(stderr, "torirsmapedctl: %s line %d does not parse; fix it by hand first\n",
            EDITOR_ENCOUNTERS_FILE_PATH, parsed.line);
        Editor_EncountersFree(out);
        ToriMem_Free(ToriMem_Editor(), copy);
        return 0;
    }
    *out_text = copy;
    *out_length = length;
    return 1;
}

/** The document from `old` to `next`, as ONE stroke of splices. */
static int
ctl_encounters_send(
    struct EditorHost* host,
    const char* old,
    size_t old_length,
    const char* next,
    size_t next_length,
    const char* what)
{
    struct Editor_Cmd* commands = ToriMem_Calloc(ToriMem_Editor(), CTL_ENCOUNTERS_PLAN_MAX, sizeof(*commands));
    int refused = 0;
    int count;

    assert(commands);
    count = Editor_StampSplices(
        EDITOR_ENCOUNTERS_DOC_KEY, old, old_length, next, next_length, commands, CTL_ENCOUNTERS_PLAN_MAX);
    if( count < 0 )
    {
        fprintf(stderr, "torirsmapedctl: %s is too large a change to send\n", what);
        ToriMem_Free(ToriMem_Editor(), commands);
        return 1;
    }
    if( count == 0 )
    {
        fprintf(stderr, "torirsmapedctl: %s: nothing changes\n", what);
        ToriMem_Free(ToriMem_Editor(), commands);
        return 1;
    }
    Editor_HostMapEdStroke(host, 1);
    for( int i = 0; i < count && !refused; i++ )
    {
        enum EditorHost_Status const status = Editor_HostMapEdCmd(host, &commands[i]);
        if( status != EDITOR_HOST_OK )
        {
            fprintf(stderr, "torirsmapedctl: %s was refused (status %d)\n", what, (int)status);
            refused = 1;
        }
    }
    Editor_HostMapEdStroke(host, 0);
    ToriMem_Free(ToriMem_Editor(), commands);
    if( !refused )
        printf("%s (unsaved until save)\n", what);
    return refused;
}

int
ToriRSMapEdCtl_Encounters(struct EditorHost* host)
{
    struct Editor_EncountersFile file;
    char* text = NULL;
    size_t length = 0;

    assert(host);
    if( !ctl_encounters_load(host, &file, &text, &length) )
        return 1;
    printf("%d encounter set(s)\n", Editor_EncountersCount(&file));
    for( int i = 0; i < file.line_count; i++ )
        if( file.lines[i].kind == EDITOR_ENCOUNTERS_LINE_RECORD )
            printf("%s\n", file.lines[i].text);
    Editor_EncountersFree(&file);
    ToriMem_Free(ToriMem_Editor(), text);
    return 0;
}

/** "npc:goblin" "3200" "3200" "0" ["5"] at args[*at]; advances *at. */
static int
ctl_parse_spawn(
    const char* const* args,
    int count,
    int* at,
    struct Editor_EncounterSpawn* out)
{
    const char* colon;
    int i = *at;

    memset(out, 0, sizeof(*out));
    out->count = 1;
    if( i + 4 > count )
        return 0;
    colon = strchr(args[i], ':');
    if( !colon || (size_t)(colon - args[i]) != 3 )
        return 0;
    if( strncmp(args[i], "npc", 3) == 0 )
        out->is_obj = 0;
    else if( strncmp(args[i], "obj", 3) == 0 )
        out->is_obj = 1;
    else
        return 0;
    if( strlen(colon + 1) >= sizeof(out->name) )
        return 0;
    snprintf(out->name, sizeof(out->name), "%s", colon + 1);
    for( int k = 1; k <= 3; k++ )
        if( !args[i + k][0] || strspn(args[i + k], "0123456789") != strlen(args[i + k]) )
            return 0;
    out->x = atoi(args[i + 1]);
    out->z = atoi(args[i + 2]);
    out->level = atoi(args[i + 3]);
    i += 4;
    /* A count is the next word when it is a number (a keyword is not). */
    if( i < count && args[i][0] && strspn(args[i], "0123456789") == strlen(args[i]) )
        out->count = atoi(args[i++]);
    *at = i;
    return 1;
}

int
ToriRSMapEdCtl_EncounterSet(
    struct EditorHost* host,
    const char* const* args,
    int count)
{
    static const char k_usage[] = "torirsmapedctl: encounter-set <name> [colour <0..7|->] [note <text|->] "
                                  "[rename <new>] [clear] [spawn|unspawn <npc|obj>:<name> <x> <z> <level> [count]]...\n";
    struct Editor_EncountersFile file;
    struct Editor_Encounter* set;
    char rename[EDITOR_ENCOUNTER_NAME_MAX] = "";
    char what[256];
    char* text = NULL;
    char* next = NULL;
    size_t length = 0;
    size_t next_length = 0;
    int existing;
    int result = 1;

    assert(host);
    assert(args || count == 0);
    if( count < 1 || !Editor_EncounterNameValid(args[0]) )
    {
        fprintf(stderr, "%s", k_usage);
        return 1;
    }
    if( !ctl_encounters_load(host, &file, &text, &length) )
        return 1;
    set = ToriMem_Alloc(ToriMem_Editor(), sizeof(*set));
    assert(set);
    existing = Editor_EncountersFind(&file, args[0]);
    if( existing >= 0 )
        *set = *file.lines[existing].encounter;
    else
        Editor_EncounterInit(set, args[0]);

    for( int i = 1; i < count; )
    {
        const char* word = args[i];
        if( strcmp(word, "clear") == 0 )
        {
            set->spawn_count = 0;
            i++;
        }
        else if( strcmp(word, "colour") == 0 && i + 1 < count )
        {
            if( strcmp(args[i + 1], "-") == 0 )
                set->colour = -1;
            else if( strlen(args[i + 1]) == 1 && args[i + 1][0] >= '0' && args[i + 1][0] < '0' + EDITOR_ENCOUNTER_COLOURS )
                set->colour = args[i + 1][0] - '0';
            else
            {
                fprintf(stderr, "torirsmapedctl: a colour is 0..%d or -\n", EDITOR_ENCOUNTER_COLOURS - 1);
                goto done;
            }
            i += 2;
        }
        else if( strcmp(word, "note") == 0 && i + 1 < count )
        {
            const char* note = strcmp(args[i + 1], "-") == 0 ? "" : args[i + 1];
            if( strlen(note) >= sizeof(set->note) || !Editor_EncounterNoteValid(note) )
            {
                fprintf(stderr, "torirsmapedctl: a note holds no '|' and is under %d bytes\n", EDITOR_ENCOUNTER_NOTE_MAX);
                goto done;
            }
            snprintf(set->note, sizeof(set->note), "%s", note);
            i += 2;
        }
        else if( strcmp(word, "rename") == 0 && i + 1 < count )
        {
            if( !Editor_EncounterNameValid(args[i + 1]) )
            {
                fprintf(stderr, "torirsmapedctl: '%s' is not a set name ([A-Za-z0-9_-], under %d)\n", args[i + 1],
                    EDITOR_ENCOUNTER_NAME_MAX);
                goto done;
            }
            snprintf(rename, sizeof(rename), "%s", args[i + 1]);
            i += 2;
        }
        else if( strcmp(word, "spawn") == 0 || strcmp(word, "unspawn") == 0 )
        {
            struct Editor_EncounterSpawn spawn;
            int const adding = word[0] == 's';
            i++;
            if( !ctl_parse_spawn(args, count, &i, &spawn) )
            {
                fprintf(stderr, "torirsmapedctl: a spawn is <npc|obj>:<name> <x> <z> <level> [count]\n");
                goto done;
            }
            if( adding && !Editor_EncounterAddSpawn(set, &spawn) )
            {
                fprintf(stderr, "torirsmapedctl: %s is in the set already, not a spawn row, or the set is full (%d)\n",
                    spawn.name, EDITOR_ENCOUNTER_SPAWNS_MAX);
                goto done;
            }
            if( !adding )
            {
                int const index = Editor_EncounterFindSpawn(set, &spawn);
                if( index < 0 )
                {
                    fprintf(stderr, "torirsmapedctl: the set has no spawn %s at %d %d %d\n", spawn.name, spawn.x,
                        spawn.z, spawn.level);
                    goto done;
                }
                Editor_EncounterRemoveSpawn(set, index);
            }
        }
        else
        {
            fprintf(stderr, "%s", k_usage);
            goto done;
        }
    }
    if( rename[0] )
        snprintf(set->name, sizeof(set->name), "%s", rename);

    if( !Editor_EncountersTextWith(&file, existing >= 0 ? args[0] : NULL, set, NULL, 0, &next_length) )
    {
        fprintf(stderr, "torirsmapedctl: set '%s' is not one the file can hold, or the name is taken\n", set->name);
        goto done;
    }
    next = ToriMem_Alloc(ToriMem_Editor(), next_length + 1);
    assert(next);
    Editor_EncountersTextWith(&file, existing >= 0 ? args[0] : NULL, set, next, next_length + 1, &next_length);
    snprintf(what, sizeof(what), "%s encounter %s (%d spawn%s)", existing >= 0 ? "set" : "new", set->name,
        set->spawn_count, set->spawn_count == 1 ? "" : "s");
    result = ctl_encounters_send(host, text, length, next, next_length, what);

done:
    Editor_EncountersFree(&file);
    ToriMem_Free(ToriMem_Editor(), set);
    ToriMem_Free(ToriMem_Editor(), text);
    ToriMem_Free(ToriMem_Editor(), next);
    return result;
}

int
ToriRSMapEdCtl_EncounterRemove(
    struct EditorHost* host,
    const char* name)
{
    struct Editor_EncountersFile file;
    char what[128];
    char* text = NULL;
    char* next;
    size_t length = 0;
    size_t next_length = 0;
    int result;

    assert(host);
    assert(name);
    if( !ctl_encounters_load(host, &file, &text, &length) )
        return 1;
    if( !Editor_EncountersTextWithout(&file, name, NULL, 0, &next_length) )
    {
        fprintf(stderr, "torirsmapedctl: there is no encounter set named '%s'\n", name);
        Editor_EncountersFree(&file);
        ToriMem_Free(ToriMem_Editor(), text);
        return 1;
    }
    next = ToriMem_Alloc(ToriMem_Editor(), next_length + 1);
    assert(next);
    Editor_EncountersTextWithout(&file, name, next, next_length + 1, &next_length);
    snprintf(what, sizeof(what), "removed encounter %.60s", name);
    result = ctl_encounters_send(host, text, length, next, next_length, what);
    Editor_EncountersFree(&file);
    ToriMem_Free(ToriMem_Editor(), text);
    ToriMem_Free(ToriMem_Editor(), next);
    return result;
}
