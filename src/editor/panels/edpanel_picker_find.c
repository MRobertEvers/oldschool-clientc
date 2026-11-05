/*
 * The square picker's Find, parsed and resolved (edpanel_picker_find.h).
 * Pure: the text and the atlas in, a query and a match out.
 */

#include "edpanel_picker_find.h"
#include "editor/editor_types.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int
find_iequals(
    const char* a,
    const char* b)
{
    while( *a && *b )
    {
        if( tolower((unsigned char)*a) != tolower((unsigned char)*b) )
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int
find_icontains(
    const char* haystack,
    const char* needle)
{
    size_t const n = strlen(needle);

    if( n == 0 )
        return 0;
    for( const char* at = haystack; *at; at++ )
    {
        size_t k = 0;
        while( k < n && at[k] && tolower((unsigned char)at[k]) == tolower((unsigned char)needle[k]) )
            k++;
        if( k == n )
            return 1;
    }
    return 0;
}

/** `text` without the blanks at either end, into `out`. Returns its length. */
static size_t
find_trim(
    const char* text,
    char out[EDITOR_PICKER_NAME_MAX])
{
    size_t length;

    while( *text == ' ' || *text == '\t' )
        text++;
    snprintf(out, EDITOR_PICKER_NAME_MAX, "%s", text);
    length = strlen(out);
    while( length > 0 && (out[length - 1] == ' ' || out[length - 1] == '\t') )
        out[--length] = '\0';
    return length;
}

/** Up to three non-negative integers separated by blanks or commas and
 *  nothing else: how many, or 0 when the text is anything else (or a fourth
 *  number follows). */
static int
find_numbers(
    const char* text,
    int out[3])
{
    int count = 0;
    const char* at = text;

    for( ;; )
    {
        long value = 0;
        int digits = 0;

        while( *at == ' ' || *at == ',' || *at == '\t' )
            at++;
        if( *at == '\0' )
            break;
        while( *at >= '0' && *at <= '9' )
        {
            value = value * 10 + (*at - '0');
            if( value > 100000 )
                return 0;
            at++;
            digits++;
        }
        if( digits == 0 || count == 3 )
            return 0;
        if( *at != '\0' && *at != ' ' && *at != ',' && *at != '\t' )
            return 0;
        out[count++] = (int)value;
    }
    return count;
}

/** "lane:" / "instance:" (any case) at the start of `text`: the rest. */
static const char*
find_prefix(
    const char* text,
    const char* prefix)
{
    size_t const n = strlen(prefix);

    for( size_t i = 0; i < n; i++ )
        if( tolower((unsigned char)text[i]) != prefix[i] )
            return NULL;
    return text + n;
}

int
Editor_PickerFindParse(
    const char* text,
    struct Editor_PickerFindQuery* out)
{
    char trimmed[EDITOR_PICKER_NAME_MAX];
    const char* rest;
    int numbers[3];
    int n;

    assert(text);
    assert(out);

    memset(out, 0, sizeof(*out));
    if( find_trim(text, trimmed) == 0 )
        return 0;

    if( (rest = find_prefix(trimmed, "lane:")) != NULL )
    {
        if( find_trim(rest, out->name) == 0 )
            return 0;
        out->kind = EDITOR_PICKER_FIND_LANE;
        return 1;
    }
    if( (rest = find_prefix(trimmed, "instance:")) != NULL )
    {
        (void)find_trim(rest, out->name);
        out->kind = EDITOR_PICKER_FIND_INSTANCE;
        return 1;
    }
    if( (trimmed[0] == 'm' || trimmed[0] == 'M') && trimmed[1] >= '0' && trimmed[1] <= '9' )
    {
        trimmed[0] = 'm';
        if( !Editor_SquareKeyParse(trimmed, &out->map_x, &out->map_z) )
            return 0;
        if( out->map_x > 255 || out->map_z > 255 )
            return 0;
        out->kind = EDITOR_PICKER_FIND_SQUARE;
        return 1;
    }
    if( (n = find_numbers(trimmed, numbers)) >= 2 )
    {
        /* Two small numbers are a square, anything at tile scale is a tile:
         * no square coordinate reaches 256, no surface tile is below it. */
        if( n == 2 && numbers[0] < 256 && numbers[1] < 256 )
        {
            out->kind = EDITOR_PICKER_FIND_SQUARE;
            out->map_x = numbers[0];
            out->map_z = numbers[1];
            return 1;
        }
        if( n == 3 && numbers[2] > 3 )
            return 0;
        if( numbers[0] >= 256 * EDITOR_SQUARE_X || numbers[1] >= 256 * EDITOR_SQUARE_Z )
            return 0;
        out->kind = EDITOR_PICKER_FIND_TILE;
        out->tile_x = numbers[0];
        out->tile_z = numbers[1];
        out->level = n == 3 ? numbers[2] : 0;
        out->map_x = numbers[0] / EDITOR_SQUARE_X;
        out->map_z = numbers[1] / EDITOR_SQUARE_Z;
        return 1;
    }
    /* One number alone, or digits run into letters ("3222x"), name nothing:
     * no area is called that. */
    if( n == 1 || (trimmed[0] >= '0' && trimmed[0] <= '9') )
        return 0;
    snprintf(out->name, sizeof(out->name), "%s", trimmed);
    out->kind = EDITOR_PICKER_FIND_NAME;
    return 1;
}

int
Editor_PickerLaneName(
    const char* storage,
    char out[EDITOR_PICKER_NAME_MAX])
{
    const char* end;
    size_t length;

    assert(storage);
    assert(out);
    out[0] = '\0';
    if( strncmp(storage, "ported/", 7) != 0 )
        return 0;
    end = strchr(storage + 7, '/');
    length = end ? (size_t)(end - (storage + 7)) : strlen(storage + 7);
    if( length == 0 || length >= EDITOR_PICKER_NAME_MAX )
        return 0;
    memcpy(out, storage + 7, length);
    out[length] = '\0';
    return 1;
}

/** A name against the Area pages and the collections: exact first (a page,
 *  then a collection), then the first that contains it (likewise). */
static int
find_name(
    const struct Editor_Picker* picker,
    const char* name,
    const char* const* collections,
    int collection_count,
    struct Editor_PickerFindMatch* out)
{
    for( int pass = 0; pass < 2; pass++ )
    {
        int (*const same)(const char*, const char*) = pass == 0 ? find_iequals : find_icontains;
        for( int t = 0; t < picker->tab_count; t++ )
        {
            int const a = picker->tabs[t].area;
            if( same(picker->tabs[t].title, name) || (a >= 0 && same(picker->areas[a].internal_name, name))
                || (a >= 0 && same(picker->areas[a].display_name, name)) )
            {
                out->kind = EDITOR_PICKER_MATCH_AREA;
                out->tab = t;
                return 1;
            }
        }
        for( int c = 0; c < collection_count; c++ )
        {
            assert(collections[c]);
            if( same(collections[c], name) )
            {
                out->kind = EDITOR_PICKER_MATCH_COLLECTION;
                out->collection = c;
                return 1;
            }
        }
    }
    return 0;
}

int
Editor_PickerFindResolve(
    const struct Editor_Picker* picker,
    const struct Editor_PickerFindQuery* query,
    const char* const* collections,
    int collection_count,
    struct Editor_PickerFindMatch* out)
{
    assert(picker);
    assert(query);
    assert(out);
    assert(collection_count >= 0);
    if( collection_count > 0 )
        assert(collections);

    memset(out, 0, sizeof(*out));
    out->tab = picker->active_tab;
    out->square = -1;
    out->collection = -1;
    out->storage = -1;

    switch( query->kind )
    {
    case EDITOR_PICKER_FIND_SQUARE:
    case EDITOR_PICKER_FIND_TILE:
        out->kind = EDITOR_PICKER_MATCH_CELL;
        out->map_x = query->map_x;
        out->map_z = query->map_z;
        if( query->kind == EDITOR_PICKER_FIND_TILE )
        {
            out->pin_valid = 1;
            out->pin_x = query->tile_x;
            out->pin_z = query->tile_z;
            out->pin_level = query->level;
        }
        /* The page's own storage first: from a lane's page, m16_101 is the
         * lane's copy. */
        if( picker->storage_filter >= 0 )
            out->square = Editor_PickerFind(picker, query->map_x, query->map_z, picker->storage_filter);
        if( out->square < 0 )
            out->square = Editor_PickerFind(picker, query->map_x, query->map_z, -1);
        /* Stay on the current page when it shows the square: a search from
         * a dungeon page for a square that dungeon claims does not jump. */
        if( out->square >= 0 && picker->tab_count > 0
            && !Editor_PickerSquareOnTab(picker, out->square, picker->active_tab) )
            out->tab = Editor_PickerTabOfSquare(picker, out->square);
        return 1;

    case EDITOR_PICKER_FIND_NAME:
        return find_name(picker, query->name, collections, collection_count, out);

    case EDITOR_PICKER_FIND_LANE:
    {
        char lane[EDITOR_PICKER_NAME_MAX];
        for( int pass = 0; pass < 2; pass++ )
            for( int s = 0; s < picker->storage_count; s++ )
            {
                if( !Editor_PickerLaneName(picker->storages[s], lane) )
                    continue;
                if( pass == 0 ? find_iequals(lane, query->name) : find_icontains(lane, query->name) )
                {
                    out->kind = EDITOR_PICKER_MATCH_LANE;
                    out->storage = s;
                    return 1;
                }
            }
        return 0;
    }

    case EDITOR_PICKER_FIND_INSTANCE:
        if( query->name[0] )
            return find_name(picker, query->name, collections, collection_count, out);
        out->kind = EDITOR_PICKER_MATCH_INSTANCE;
        return 1;

    case EDITOR_PICKER_FIND_NONE:
        break;
    }
    return 0;
}
