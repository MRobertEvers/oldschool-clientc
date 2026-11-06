/*
 * The npc stats ledger (editor_npcstats.h): the line-preserving codec, the
 * one edit the Stats tab makes, and the derived cells.
 */

#include "editor_npcstats.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const k_levels[EDITOR_NPCSTATS_LEVELS] = {
    "hitpoints", "attack", "strength", "defence", "magic", "ranged",
};

static const char* const k_bonuses[EDITOR_NPCSTATS_BONUSES] = {
    "stabattack",  "slashattack", "crushattack", "stabdefence",  "slashdefence", "crushdefence", "strengthbonus",
    "rangeattack", "rangebonus",  "magicattack", "magicdefence", "rangedefence", "magic_maxhit",
};

/* ---- names ------------------------------------------------------------------- */

char
Editor_NpcStatsShard(const char* name)
{
    char c;

    assert(name);
    assert(name[0]);
    c = name[0];
    if( c >= 'A' && c <= 'Z' )
        c = (char)(c - 'A' + 'a');
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ? c : '_';
}

int
Editor_NpcStatsNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_CMD_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

void
Editor_NpcStatsDocKey(
    char out[EDITOR_DOC_KEY_MAX],
    const char* name)
{
    assert(out);
    assert(name);
    assert(Editor_NpcStatsNameValid(name));
    snprintf(out, EDITOR_DOC_KEY_MAX, "%s%c/%s", EDITOR_NPCSTATS_KEY_PREFIX, Editor_NpcStatsShard(name), name);
}

void
Editor_NpcStatsPath(
    char* out,
    size_t out_size,
    const char* name)
{
    assert(out);
    assert(name);
    assert(Editor_NpcStatsNameValid(name));
    snprintf(out, out_size, "%s/%c/%s.stats", EDITOR_NPCSTATS_DIR, Editor_NpcStatsShard(name), name);
}

/* ---- the codec ---------------------------------------------------------------- */

void
Editor_NpcStatsInit(struct Editor_NpcStatsFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

void
Editor_NpcStatsFree(struct Editor_NpcStatsFile* file)
{
    if( !file )
        return;
    for( int i = 0; i < file->line_count; i++ )
        ToriMem_Free(ToriMem_Editor(), file->lines[i].text);
    ToriMem_Free(ToriMem_Editor(), file->lines);
    memset(file, 0, sizeof(*file));
}

static int
is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/** Classify `line->text` (read_ledger's rule). */
static void
classify(struct Editor_NpcStatsLine* line)
{
    const char* text = line->text;
    int const length = (int)strlen(text);
    int lead = 0;
    int eq;
    int note = -1;
    int key_end;
    int value_begin;
    int value_end;

    line->kind = EDITOR_NPCSTATS_LINE_TEXT;
    line->key[0] = '\0';
    line->note_at = -1;
    line->value_begin = line->value_end = 0;
    while( lead < length && is_blank(text[lead]) )
        lead++;
    if( lead + 1 < length && text[lead] == '/' && text[lead + 1] == '/' )
        return;
    eq = -1;
    for( int i = 0; i < length; i++ )
        if( text[i] == '=' )
        {
            eq = i;
            break;
        }
    if( eq < 0 )
        return;
    /* The note: the first "//" after the '=' (read_ledger partitions the
     * whole line on it; a "//" before the '=' would be a comment line). */
    for( int i = eq + 1; i + 1 < length; i++ )
        if( text[i] == '/' && text[i + 1] == '/' )
        {
            note = i;
            break;
        }
    key_end = eq;
    while( key_end > lead && is_blank(text[key_end - 1]) )
        key_end--;
    if( key_end - lead <= 0 || key_end - lead >= EDITOR_NPCSTATS_KEY_MAX )
        return;
    value_begin = eq + 1;
    value_end = note >= 0 ? note : length;
    while( value_begin < value_end && is_blank(text[value_begin]) )
        value_begin++;
    while( value_end > value_begin && is_blank(text[value_end - 1]) )
        value_end--;
    line->kind = EDITOR_NPCSTATS_LINE_FIELD;
    memcpy(line->key, text + lead, (size_t)(key_end - lead));
    line->key[key_end - lead] = '\0';
    line->value_begin = value_begin;
    line->value_end = value_end;
    line->note_at = note;
}

static struct Editor_NpcStatsLine*
append_line(
    struct Editor_NpcStatsFile* file,
    const char* text,
    size_t length)
{
    struct Editor_NpcStatsLine* line;

    if( file->line_count == file->line_capacity )
    {
        int const next = file->line_capacity ? file->line_capacity * 2 : 64;
        file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, (size_t)next * sizeof(*file->lines));
        assert(file->lines);
        file->line_capacity = next;
    }
    line = &file->lines[file->line_count++];
    memset(line, 0, sizeof(*line));
    line->text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(line->text);
    if( length > 0 )
        memcpy(line->text, text, length);
    line->text[length] = '\0';
    classify(line);
    return line;
}

void
Editor_NpcStatsParse(
    struct Editor_NpcStatsFile* file,
    const char* text,
    size_t length)
{
    size_t at = 0;

    assert(file);
    assert(text || length == 0);
    Editor_NpcStatsFree(file);
    while( at < length )
    {
        const char* begin = text + at;
        const char* newline = memchr(begin, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - begin) : length - at;

        (void)append_line(file, begin, line_length);
        at += line_length;
        if( newline )
        {
            at++;
            file->final_newline = at == length;
        }
        else
            file->final_newline = 0;
    }
}

static void
put(
    char* out,
    size_t capacity,
    size_t* total,
    const char* text,
    size_t length)
{
    if( out && *total < capacity )
    {
        size_t const room = capacity - *total;
        memcpy(out + *total, text, length < room ? length : room);
    }
    *total += length;
}

static void
put_terminate(
    char* out,
    size_t capacity,
    size_t total)
{
    if( out && capacity > 0 )
        out[total < capacity ? total : capacity - 1] = '\0';
}

size_t
Editor_NpcStatsEmit(
    const struct Editor_NpcStatsFile* file,
    char* out,
    size_t out_capacity)
{
    size_t total = 0;

    assert(file);
    assert(out || out_capacity == 0);
    for( int i = 0; i < file->line_count; i++ )
    {
        put(out, out_capacity, &total, file->lines[i].text, strlen(file->lines[i].text));
        if( i + 1 < file->line_count || file->final_newline )
            put(out, out_capacity, &total, "\n", 1);
    }
    put_terminate(out, out_capacity, total);
    return total;
}

int
Editor_NpcStatsFind(
    const struct Editor_NpcStatsFile* file,
    const char* key)
{
    assert(file);
    assert(key);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].kind == EDITOR_NPCSTATS_LINE_FIELD && strcmp(file->lines[i].key, key) == 0 )
            return i;
    return -1;
}

int
Editor_NpcStatsGet(
    const struct Editor_NpcStatsFile* file,
    const char* key,
    char* out,
    size_t out_size)
{
    const struct Editor_NpcStatsLine* line;
    int index;
    int length;

    assert(file);
    assert(key);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    index = Editor_NpcStatsFind(file, key);
    if( index < 0 )
        return 0;
    line = &file->lines[index];
    length = line->value_end - line->value_begin;
    if( length <= 0 || (length == 1 && line->text[line->value_begin] == '-') )
        return 0;
    snprintf(out, out_size, "%.*s", length, line->text + line->value_begin);
    return 1;
}

/* ---- the edit ----------------------------------------------------------------- */

static int
key_ok(const char* key)
{
    size_t const length = strlen(key);
    if( length == 0 || length >= EDITOR_NPCSTATS_KEY_MAX || strstr(key, "//") )
        return 0;
    for( size_t i = 0; i < length; i++ )
        if( key[i] == '=' || key[i] == ' ' || key[i] == '\t' || (unsigned char)key[i] < 0x20 )
            return 0;
    return 1;
}

static int
value_ok(const char* value)
{
    size_t const length = strlen(value);
    if( length >= EDITOR_NPCSTATS_VALUE_MAX || strstr(value, "//") )
        return 0;
    if( length > 0 && (value[0] == ' ' || value[length - 1] == ' ') )
        return 0;
    for( size_t i = 0; i < length; i++ )
        if( (unsigned char)value[i] < 0x20 )
            return 0;
    return 1;
}

/** Line `line` with its value replaced: the key column kept, the gap to a
 *  note shrunk or grown so the note keeps its column where it can (one space
 *  at least), the rest kept. Owned result. */
static char*
line_with_value(
    const struct Editor_NpcStatsLine* line,
    const char* value)
{
    size_t const length = strlen(line->text);
    size_t const new_value = strlen(value);
    size_t const old_value = (size_t)(line->value_end - line->value_begin);
    size_t const prefix = (size_t)line->value_begin;
    /* A line stating no value ("key =") gains the space after its '='. */
    int const space = old_value == 0 && prefix > 0 && line->text[prefix - 1] == '=';
    size_t rest_at = (size_t)line->value_end;
    size_t gap = 0;
    size_t total;
    size_t at;
    char* out;

    if( line->note_at >= 0 )
    {
        long const old_gap = (long)line->note_at - (long)rest_at;
        long const want = old_gap - ((long)new_value - (long)old_value);
        gap = want < 1 ? 1 : (size_t)want;
        rest_at = (size_t)line->note_at;
    }
    total = prefix + (size_t)space + new_value + gap + (length - rest_at);
    out = ToriMem_Alloc(ToriMem_Editor(), total + 1);
    assert(out);
    memcpy(out, line->text, prefix);
    at = prefix;
    if( space )
        out[at++] = ' ';
    memcpy(out + at, value, new_value);
    at += new_value;
    memset(out + at, ' ', gap);
    at += gap;
    memcpy(out + at, line->text + rest_at, length - rest_at);
    at += length - rest_at;
    out[at] = '\0';
    assert(at == total);
    return out;
}

/** A new `key = value` line in the key column of `like` (its '=' position),
 *  with its CR when `like` has one. Owned. */
static char*
line_new(
    const struct Editor_NpcStatsLine* like,
    const char* key,
    const char* value)
{
    int width = (int)strlen(key);
    int cr = 0;
    char* out;
    int n;

    if( like )
    {
        const char* eq = strchr(like->text, '=');
        int const column = eq ? (int)(eq - like->text) : 0;
        /* "key<pad> = value": the '=' one past the padded key. */
        if( column - 1 > width )
            width = column - 1;
        cr = strlen(like->text) > 0 && like->text[strlen(like->text) - 1] == '\r';
    }
    n = snprintf(NULL, 0, "%-*s = %s%s", width, key, value, cr ? "\r" : "");
    out = ToriMem_Alloc(ToriMem_Editor(), (size_t)n + 1);
    assert(out);
    snprintf(out, (size_t)n + 1, "%-*s = %s%s", width, key, value, cr ? "\r" : "");
    return out;
}

/** Replace line `index`'s text (owned `text`), or remove it (`text` NULL). */
static void
set_line(
    struct Editor_NpcStatsFile* file,
    int index,
    char* text)
{
    ToriMem_Free(ToriMem_Editor(), file->lines[index].text);
    if( text )
    {
        file->lines[index].text = text;
        classify(&file->lines[index]);
        return;
    }
    memmove(&file->lines[index], &file->lines[index + 1],
        (size_t)(file->line_count - index - 1) * sizeof(file->lines[0]));
    file->line_count--;
}

static void
insert_line(
    struct Editor_NpcStatsFile* file,
    int index,
    char* text)
{
    if( file->line_count == file->line_capacity )
    {
        int const next = file->line_capacity ? file->line_capacity * 2 : 64;
        file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, (size_t)next * sizeof(*file->lines));
        assert(file->lines);
        file->line_capacity = next;
    }
    memmove(&file->lines[index + 1], &file->lines[index], (size_t)(file->line_count - index) * sizeof(file->lines[0]));
    file->line_count++;
    memset(&file->lines[index], 0, sizeof(file->lines[0]));
    file->lines[index].text = text;
    classify(&file->lines[index]);
}

/** Where a new stat line goes: after the last field before the checks
 *  section (a "// Checks" comment), the `source` line excepted when other
 *  fields follow it; after the source line when there are none; at the end
 *  of an empty file. */
static int
insert_point(const struct Editor_NpcStatsFile* file)
{
    int stop = file->line_count;
    int last = -1;

    for( int i = 0; i < file->line_count; i++ )
    {
        const char* text = file->lines[i].text;
        while( is_blank(*text) )
            text++;
        if( file->lines[i].kind == EDITOR_NPCSTATS_LINE_TEXT && strncmp(text, "// Checks", 9) == 0 )
        {
            stop = i;
            break;
        }
    }
    for( int i = 0; i < stop; i++ )
        if( file->lines[i].kind == EDITOR_NPCSTATS_LINE_FIELD )
            last = i;
    return last >= 0 ? last + 1 : stop;
}

int
Editor_NpcStatsTextSet(
    const char* old_text,
    size_t old_length,
    const char* npc,
    const char* key,
    const char* value,
    char* out,
    size_t out_capacity,
    size_t* out_length)
{
    struct Editor_NpcStatsFile file;
    int index;

    assert(old_text || old_length == 0);
    assert(npc);
    assert(key);
    assert(value);
    assert(out || out_capacity == 0);
    assert(out_length);
    if( !key_ok(key) || !value_ok(value) || strcmp(key, "source") == 0 )
        return 0;

    Editor_NpcStatsInit(&file);
    if( old_length == 0 )
    {
        /* A ledger the generator never wrote: say who did, and pin it. */
        char header[512];
        int const n = snprintf(header, sizeof(header),
            "// %s -- stats authored in ToriRSMapEd's encounter builder (Stats tab).\n"
            "// `source = authored`: tools/gen_npc_stats.py reads this file back and\n"
            "// leaves it exactly as it stands.\n"
            "\n"
            "source = authored\n",
            npc);
        assert(n > 0 && (size_t)n < sizeof(header));
        Editor_NpcStatsParse(&file, header, (size_t)n);
        /* The stat block after a blank line. */
        insert_line(&file, file.line_count, ToriMem_Strdup(ToriMem_Editor(), ""));
        assert(file.lines[file.line_count - 1].text);
    }
    else
        Editor_NpcStatsParse(&file, old_text, old_length);

    /* The value. */
    index = Editor_NpcStatsFind(&file, key);
    if( index >= 0 && !value[0] )
        set_line(&file, index, NULL);
    else if( index >= 0 )
        set_line(&file, index, line_with_value(&file.lines[index], value));
    else if( value[0] )
    {
        int const at = old_length == 0 ? file.line_count : insert_point(&file);
        const struct Editor_NpcStatsLine* like = NULL;
        for( int i = at - 1; i >= 0 && !like; i-- )
            if( file.lines[i].kind == EDITOR_NPCSTATS_LINE_FIELD && strcmp(file.lines[i].key, "source") != 0 )
                like = &file.lines[i];
        insert_line(&file, at, line_new(like, key, value));
    }

    /* `source = authored`, the pin. */
    index = Editor_NpcStatsFind(&file, "source");
    if( index >= 0 )
    {
        const struct Editor_NpcStatsLine* line = &file.lines[index];
        int const length = line->value_end - line->value_begin;
        if( length != 8 || memcmp(line->text + line->value_begin, "authored", 8) != 0 )
            set_line(&file, index, line_with_value(line, "authored"));
    }
    else
    {
        int first = 0;
        while( first < file.line_count && file.lines[first].kind != EDITOR_NPCSTATS_LINE_FIELD )
            first++;
        insert_line(&file, first, line_new(NULL, "source", "authored"));
    }
    if( old_length == 0 )
        file.final_newline = 1;

    *out_length = Editor_NpcStatsEmit(&file, out, out_capacity);
    Editor_NpcStatsFree(&file);
    return 1;
}

/* ---- the Stats tab ------------------------------------------------------------- */

const char*
Editor_NpcStatsLevelKey(int index)
{
    assert(index >= 0);
    assert(index < EDITOR_NPCSTATS_LEVELS);
    return k_levels[index];
}

const char*
Editor_NpcStatsBonusKey(int index)
{
    assert(index >= 0);
    assert(index < EDITOR_NPCSTATS_BONUSES);
    return k_bonuses[index];
}

int
Editor_NpcMaxHit(
    int level,
    int bonus)
{
    long const product = (long)(level + 9) * (long)(bonus + 64);
    if( product <= 0 )
        return 0;
    return (int)((product + 320) / 640);
}

int
Editor_NpcCombatLevel(
    int attack,
    int strength,
    int defence,
    int hitpoints,
    int magic,
    int ranged)
{
    /* ToriRSServer_CombatLevel's integers, prayer 0. */
    int const base = defence + hitpoints;
    int const melee = attack + strength;
    int const ranged_term = (ranged * 3) / 2;
    int const magic_term = (magic * 3) / 2;
    int best = melee;

    if( ranged_term > best )
        best = ranged_term;
    if( magic_term > best )
        best = magic_term;
    return ((base * 250) + (best * 325)) / 1000;
}
