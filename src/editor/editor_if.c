#include "editor_if.h"

#include "editor_doc.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the vocabulary ------------------------------------------------------ *
 *
 * cachepack's emit_component order (3rd/rscache/tools/cachepack/cp_decode.c),
 * row for row; read_component_line is the same set. editor_if_test fails on a
 * key the tree holds that is not here, and on a block whose keys are not in
 * this order.
 */

#define K_INT EDITOR_IF_VALUE_INT
#define K_BOOL EDITOR_IF_VALUE_BOOL
#define K_STR EDITOR_IF_VALUE_STR
#define K_HOOK EDITOR_IF_VALUE_HOOK
#define K_TRIG EDITOR_IF_VALUE_TRIGGERS

#define G_ID EDITOR_IF_GROUP_IDENTITY
#define G_GEO EDITOR_IF_GROUP_GEOMETRY
#define G_GFX EDITOR_IF_GROUP_GRAPHIC
#define G_TXT EDITOR_IF_GROUP_TEXT
#define G_MDL EDITOR_IF_GROUP_MODEL
#define G_INV EDITOR_IF_GROUP_INVENTORY
#define G_ACT EDITOR_IF_GROUP_INTERACTION
#define G_SCR EDITOR_IF_GROUP_SCRIPTS

static const struct Editor_IfKeyInfo k_keys[] = {
    { "empty", EDITOR_IF_VALUE_MARKER, G_ID, 0, 0, "empty member" },
    { "if3", K_BOOL, G_ID, 0, 0, "IF3" },
    { "type", K_INT, G_ID, 0, 0, "type" },
    { "clientcode", K_INT, G_ID, 0, 0, "client code" },
    { "x", K_INT, G_GEO, 0, 0, "x" },
    { "y", K_INT, G_GEO, 0, 0, "y" },
    { "width", K_INT, G_GEO, 0, 0, "width" },
    { "height", K_INT, G_GEO, 0, 0, "height" },
    { "widthmode", K_INT, G_GEO, 0, 0, "width mode" },
    { "heightmode", K_INT, G_GEO, 0, 0, "height mode" },
    { "xmode", K_INT, G_GEO, 0, 0, "x mode" },
    { "ymode", K_INT, G_GEO, 0, 0, "y mode" },
    { "layer", K_INT, G_GEO, 0, 0, "parent (layer)" },
    { "hidden", K_BOOL, G_GFX, 0, 0, "hidden" },
    { "trans", K_INT, G_GFX, 0, 0, "transparency" },
    { "colour", K_INT, G_GFX, 0, 0, "colour" },
    { "fill", K_BOOL, G_GFX, 0, 0, "filled" },
    { "alpha", K_BOOL, G_GFX, 0, 0, "alpha" },
    { "tiled", K_BOOL, G_GFX, 0, 0, "tiled" },
    { "outline", K_INT, G_GFX, 0, 0, "outline" },
    { "graphic", K_INT, G_GFX, 0, 0, "graphic" },
    { "graphicshadow", K_INT, G_GFX, 0, 0, "graphic shadow" },
    { "hflip", K_BOOL, G_GFX, 0, 0, "flip h" },
    { "vflip", K_BOOL, G_GFX, 0, 0, "flip v" },
    { "angle", K_INT, G_GFX, 0, 0, "angle" },
    { "scrollwidth", K_INT, G_GEO, 0, 0, "scroll width" },
    { "scrollheight", K_INT, G_GEO, 0, 0, "scroll height" },
    { "noclickthrough", K_BOOL, G_ACT, 0, 0, "no click-through" },
    { "clickmask", K_INT, G_ACT, 0, 0, "click mask" },
    { "linewidth", K_INT, G_GFX, 0, 0, "line width" },
    { "linedirection", K_BOOL, G_GFX, 0, 0, "line direction" },
    { "buttontype", K_INT, G_ACT, 0, 0, "button type" },
    { "linked", K_INT, G_GEO, 0, 0, "over layer (linked)" },
    { "marginx", K_INT, G_INV, 0, 0, "margin x" },
    { "marginy", K_INT, G_INV, 0, 0, "margin y" },
    { "text", K_STR, G_TXT, 0, 0, "text" },
    { "name", K_STR, G_ACT, 0, 0, "name (op base)" },
    { "targetverb", K_STR, G_ACT, 0, 0, "target verb" },
    { "targettext", K_STR, G_ACT, 0, 0, "target text" },
    { "activetext", K_STR, G_TXT, 0, 0, "active text" },
    { "option", K_STR, G_ACT, 0, 0, "option" },
    { "font", K_INT, G_TXT, 0, 0, "font" },
    { "halign", K_INT, G_TXT, 0, 0, "h align" },
    { "valign", K_INT, G_TXT, 0, 0, "v align" },
    { "lineheight", K_INT, G_TXT, 0, 0, "line height" },
    { "shadow", K_BOOL, G_TXT, 0, 0, "shadow" },
    { "model", K_INT, G_MDL, 0, 0, "model" },
    { "modelzoom", K_INT, G_MDL, 0, 0, "zoom" },
    { "modelxan", K_INT, G_MDL, 0, 0, "x angle" },
    { "modelyan", K_INT, G_MDL, 0, 0, "y angle" },
    { "modelzan", K_INT, G_MDL, 0, 0, "z angle" },
    { "modelxof", K_INT, G_MDL, 0, 0, "x offset" },
    { "modelyof", K_INT, G_MDL, 0, 0, "y offset" },
    { "modelanim", K_INT, G_MDL, 0, 0, "anim" },
    { "modelortho", K_BOOL, G_MDL, 0, 0, "orthographic" },
    { "modelfixedzoom", K_BOOL, G_MDL, 0, 0, "fixed zoom" },
    { "activemodel", K_INT, G_MDL, 0, 0, "active model" },
    { "activeanim", K_INT, G_MDL, 0, 0, "active anim" },
    { "activegraphic", K_INT, G_GFX, 0, 0, "active graphic" },
    { "activecolour", K_INT, G_GFX, 0, 0, "active colour" },
    { "overcolour", K_INT, G_GFX, 0, 0, "over colour" },
    { "activeovercolour", K_INT, G_GFX, 0, 0, "active over colour" },
    { "short50", K_INT, G_MDL, 0, 0, "short50" },
    { "int5957", K_INT, G_MDL, 0, 0, "int5957" },
    { "int5920", K_INT, G_MDL, 0, 0, "int5920" },
    { "op", K_STR, G_ACT, 1, 0, "op" },
    { "objop", K_STR, G_INV, 1, 5, "obj op" },
    { "dragdeadzone", K_INT, G_ACT, 0, 0, "drag dead zone" },
    { "dragdeadtime", K_INT, G_ACT, 0, 0, "drag dead time" },
    { "dragrender", K_BOOL, G_ACT, 0, 0, "drag render" },
    { "invslot", EDITOR_IF_VALUE_INVSLOT, G_INV, 1, 20, "inv slot" },
    { "cs1cmp", EDITOR_IF_VALUE_CS1CMP, G_SCR, 1, 0, "cs1 compare" },
    { "cs1script", EDITOR_IF_VALUE_CS1SCRIPT, G_SCR, 1, 0, "cs1 script" },
    { "onload", K_HOOK, G_SCR, 0, 0, "onload" },
    { "onmouseover", K_HOOK, G_SCR, 0, 0, "onmouseover" },
    { "onmouseleave", K_HOOK, G_SCR, 0, 0, "onmouseleave" },
    { "ontargetleave", K_HOOK, G_SCR, 0, 0, "ontargetleave" },
    { "ontargetenter", K_HOOK, G_SCR, 0, 0, "ontargetenter" },
    { "onvarptransmit", K_HOOK, G_SCR, 0, 0, "onvarptransmit" },
    { "oninvtransmit", K_HOOK, G_SCR, 0, 0, "oninvtransmit" },
    { "onstattransmit", K_HOOK, G_SCR, 0, 0, "onstattransmit" },
    { "ontimer", K_HOOK, G_SCR, 0, 0, "ontimer" },
    { "onop", K_HOOK, G_SCR, 0, 0, "onop" },
    { "onmouserepeat", K_HOOK, G_SCR, 0, 0, "onmouserepeat" },
    { "onclick", K_HOOK, G_SCR, 0, 0, "onclick" },
    { "onclickrepeat", K_HOOK, G_SCR, 0, 0, "onclickrepeat" },
    { "onrelease", K_HOOK, G_SCR, 0, 0, "onrelease" },
    { "onhold", K_HOOK, G_SCR, 0, 0, "onhold" },
    { "ondrag", K_HOOK, G_SCR, 0, 0, "ondrag" },
    { "ondragcomplete", K_HOOK, G_SCR, 0, 0, "ondragcomplete" },
    { "onscrollwheel", K_HOOK, G_SCR, 0, 0, "onscrollwheel" },
    { "varptriggers", K_TRIG, G_SCR, 0, 0, "varp triggers" },
    { "invtriggers", K_TRIG, G_SCR, 0, 0, "inv triggers" },
    { "stattriggers", K_TRIG, G_SCR, 0, 0, "stat triggers" },
};

#define KEY_COUNT ((int)(sizeof(k_keys) / sizeof(k_keys[0])))
/** Ranks fold (row, index): an indexed family never reaches this many. */
#define RANK_STRIDE 4096

int
Editor_IfKeyCount(void)
{
    return KEY_COUNT;
}

const struct Editor_IfKeyInfo*
Editor_IfKeyAt(int row)
{
    assert(row >= 0);
    assert(row < KEY_COUNT);
    return &k_keys[row];
}

/** cachepack's cp_indexed_key: "<prefix><N>", N >= 1, digits only. */
static int
indexed_key(
    const char* key,
    const char* prefix)
{
    size_t prefix_length = strlen(prefix);
    int value = 0;
    const char* digits;

    if( strncmp(key, prefix, prefix_length) != 0 )
        return -1;
    digits = key + prefix_length;
    if( !*digits )
        return -1;
    for( const char* p = digits; *p; p++ )
    {
        if( *p < '0' || *p > '9' )
            return -1;
        value = value * 10 + (*p - '0');
        if( value > 100000 )
            return -1;
    }
    if( value <= 0 )
        return -1;
    return value - 1;
}

int
Editor_IfKeyLookup(
    const char* key,
    int* out_index)
{
    assert(key);

    if( out_index )
        *out_index = 0;
    for( int row = 0; row < KEY_COUNT; row++ )
    {
        if( !k_keys[row].indexed && strcmp(key, k_keys[row].key) == 0 )
            return row;
    }
    for( int row = 0; row < KEY_COUNT; row++ )
    {
        int index;

        if( !k_keys[row].indexed )
            continue;
        index = indexed_key(key, k_keys[row].key);
        if( index < 0 )
            continue;
        if( k_keys[row].index_max && index >= k_keys[row].index_max )
            return -1;
        if( index >= RANK_STRIDE )
            return -1;
        if( out_index )
            *out_index = index;
        return row;
    }
    return -1;
}

int
Editor_IfKeyRank(const char* key)
{
    int index = 0;
    int row = Editor_IfKeyLookup(key, &index);

    if( row < 0 )
        return -1;
    return row * RANK_STRIDE + index;
}

/* ---- values -------------------------------------------------------------- */

/** strtol base 0 over the whole text, in int32 range (cp_parse_int). */
static int
parse_int(
    const char* text,
    int* out)
{
    char* end = NULL;
    long value;

    if( !text || !*text )
        return 0;
    errno = 0;
    value = strtol(text, &end, 0);
    if( errno != 0 || end == text || *end != '\0' )
        return 0;
    if( value < -2147483647L - 1 || value > 2147483647L )
        return 0;
    if( out )
        *out = (int)value;
    return 1;
}

static int
parse_bool(const char* text)
{
    return strcmp(text, "yes") == 0 || strcmp(text, "true") == 0 || strcmp(text, "1") == 0
        || strcmp(text, "no") == 0 || strcmp(text, "false") == 0 || strcmp(text, "0") == 0;
}

/** `count` (or, with count < 0, any number of) comma-separated ints. */
static int
parse_int_list(
    const char* raw,
    int count)
{
    char scratch[4096];
    char* cursor;
    int seen = 0;

    if( strlen(raw) >= sizeof(scratch) )
        return 0;
    snprintf(scratch, sizeof(scratch), "%s", raw);
    cursor = scratch;
    for( ;; )
    {
        char* comma = strchr(cursor, ',');
        if( comma )
            *comma = '\0';
        if( !parse_int(cursor, NULL) )
            return 0;
        seen++;
        if( !comma )
            break;
        cursor = comma + 1;
    }
    return count < 0 || seen == count;
}

/** Is `s[at]` preceded by an odd run of backslashes? */
static int
text_escaped(
    const char* s,
    size_t at)
{
    size_t run = 0;

    while( at > run && s[at - 1 - run] == '\\' )
        run++;
    return (int)(run & 1);
}

/** The raw value survives config reading unchanged: no line break, no
 *  unescaped `//` (a comment would cut it), no unescaped trailing blank. */
static int
raw_survives(const char* raw)
{
    size_t length = strlen(raw);

    for( size_t i = 0; i < length; i++ )
    {
        if( raw[i] == '\n' || raw[i] == '\r' )
            return 0;
        if( raw[i] == '\\' && raw[i + 1] )
        {
            i++;
            continue;
        }
        if( raw[i] == '/' && raw[i + 1] == '/' )
            return 0;
    }
    if( length > 0 && (raw[length - 1] == ' ' || raw[length - 1] == '\t')
        && !text_escaped(raw, length - 1) )
        return 0;
    return 1;
}

int
Editor_IfValueValid(
    const char* key,
    const char* raw)
{
    int row;

    assert(key);
    assert(raw);

    row = Editor_IfKeyLookup(key, NULL);
    if( row < 0 || !raw_survives(raw) )
        return 0;
    switch( k_keys[row].kind )
    {
    case EDITOR_IF_VALUE_INT:
        return parse_int(raw, NULL);
    case EDITOR_IF_VALUE_BOOL:
        return parse_bool(raw);
    case EDITOR_IF_VALUE_STR:
        return 1;
    case EDITOR_IF_VALUE_HOOK:
    {
        struct Editor_IfHook* hook = ToriMem_Alloc(ToriMem_Editor(), sizeof(*hook));
        int ok;

        assert(hook);
        ok = Editor_IfHookParse(raw, hook);
        ToriMem_Free(ToriMem_Editor(), hook);
        return ok;
    }
    case EDITOR_IF_VALUE_TRIGGERS:
        return raw[0] == '\0' || parse_int_list(raw, -1);
    case EDITOR_IF_VALUE_INVSLOT:
        return parse_int_list(raw, 3);
    case EDITOR_IF_VALUE_CS1CMP:
        return parse_int_list(raw, 2);
    case EDITOR_IF_VALUE_CS1SCRIPT:
        return raw[0] == '\0' || parse_int_list(raw, -1);
    case EDITOR_IF_VALUE_MARKER:
        return 1;
    }
    return 0;
}

/* ---- the file ------------------------------------------------------------ */

#define PREAMBLE_PREFIX "// Interface "
/* U+2014 EM DASH, as cachepack writes it. */
#define PREAMBLE_DASH " \xE2\x80\x94 "
#define PREAMBLE_SUFFIX " components."

/**
 * The preamble's first line: id at [*id_at, *id_end), count at
 * [*count_at, *count_end). 0 when the line is not cachepack's.
 */
static int
preamble_parse(
    const char* text,
    size_t length,
    int* out_id,
    int* out_count,
    size_t* out_count_at,
    size_t* out_count_end)
{
    size_t prefix = strlen(PREAMBLE_PREFIX);
    size_t dash = strlen(PREAMBLE_DASH);
    size_t at;
    size_t start;
    long id = 0;
    long count = 0;
    const char* newline;
    size_t line_end;

    if( length < prefix || memcmp(text, PREAMBLE_PREFIX, prefix) != 0 )
        return 0;
    newline = memchr(text, '\n', length);
    line_end = newline ? (size_t)(newline - text) : length;
    at = prefix;
    start = at;
    while( at < line_end && text[at] >= '0' && text[at] <= '9' )
        id = id * 10 + (text[at++] - '0');
    if( at == start || at + dash > line_end || memcmp(text + at, PREAMBLE_DASH, dash) != 0 )
        return 0;
    at += dash;
    start = at;
    while( at < line_end && text[at] >= '0' && text[at] <= '9' )
        count = count * 10 + (text[at++] - '0');
    if( at == start || line_end - at != strlen(PREAMBLE_SUFFIX)
        || memcmp(text + at, PREAMBLE_SUFFIX, strlen(PREAMBLE_SUFFIX)) != 0 )
        return 0;
    if( out_id )
        *out_id = (int)id;
    if( out_count )
        *out_count = (int)count;
    if( out_count_at )
        *out_count_at = start;
    if( out_count_end )
        *out_count_end = at;
    return 1;
}

void
Editor_IfIndex(
    struct Editor_IfFile* file,
    const char* text,
    size_t length)
{
    assert(file);
    assert(text || length == 0);

    memset(file, 0, sizeof(*file));
    file->text = text;
    file->length = length;
    file->interface_id = -1;
    file->stated_count = -1;
    if( text )
        preamble_parse(text, length, &file->interface_id, &file->stated_count, NULL, NULL);
    Editor_CfgFileIndex(&file->index, text, length);
}

void
Editor_IfFree(struct Editor_IfFile* file)
{
    if( !file )
        return;
    Editor_CfgFileFree(&file->index);
    memset(file, 0, sizeof(*file));
}

void
Editor_IfBlockName(
    const struct Editor_IfFile* file,
    int position,
    char* out,
    size_t out_size)
{
    const struct Editor_CfgBlockRef* ref;

    assert(file);
    assert(out);
    assert(out_size > 0);
    assert(position >= 0);
    assert(position < file->index.block_count);

    ref = &file->index.blocks[position];
    snprintf(out, out_size, "%.*s", (int)ref->name_length, file->text + ref->name_start);
}

int
Editor_IfFind(
    const struct Editor_IfFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    return Editor_CfgFileFind(&file->index, name);
}

/** Block `position` parsed into `block` (a copy of its text). */
static void
block_load(
    const struct Editor_IfFile* file,
    int position,
    struct Editor_CfgBlock* block)
{
    const struct Editor_CfgBlockRef* ref = &file->index.blocks[position];

    Editor_CfgBlockInit(block);
    Editor_CfgBlockSet(block, file->text + ref->start, ref->end - ref->start);
}

/** Occurrence 0 of `key` in a parsed block, raw value into `out`. */
static int
block_value(
    const struct Editor_CfgBlock* block,
    const char* key,
    char* out,
    size_t out_size)
{
    int line = Editor_CfgBlockFieldFind(block, key, 0);
    size_t length;
    const char* value;

    if( line < 0 )
        return 0;
    value = Editor_CfgBlockValue(block, line, &length);
    if( out_size == 0 )
        return length > 0 ? 2 : 1;
    snprintf(out, out_size, "%.*s", (int)length, value);
    return length >= out_size ? 2 : 1;
}

int
Editor_IfBlockValue(
    const struct Editor_IfFile* file,
    int position,
    const char* key,
    char* out,
    size_t out_size)
{
    struct Editor_CfgBlock block;
    int found;

    assert(file);
    assert(key);
    assert(out || out_size == 0);

    if( position < 0 || position >= file->index.block_count )
        return 0;
    block_load(file, position, &block);
    found = block_value(&block, key, out, out_size);
    Editor_CfgBlockFree(&block);
    return found;
}

/** A `layer=` raw value as a child of interface `interface_id`: -1 absent
 *  (raw NULL), -2 foreign or unparsable. */
static int
layer_child(
    const char* raw,
    int interface_id)
{
    int value;

    if( !raw )
        return -1;
    if( !parse_int(raw, &value) )
        return -2;
    if( value < 0 )
        return -1;
    if( interface_id >= 0 && (value >> 16) != interface_id )
        return -2;
    return value & 0xFFFF;
}

int
Editor_IfBlockParent(
    const struct Editor_IfFile* file,
    int position)
{
    char raw[64];
    int found;

    assert(file);
    found = Editor_IfBlockValue(file, position, "layer", raw, sizeof(raw));
    if( found == 2 )
        return -2;
    return layer_child(found ? raw : NULL, file->interface_id);
}

int
Editor_IfEmitPreamble(
    const char* interface_name,
    int interface_id,
    int component_count,
    char* out,
    size_t out_size)
{
    assert(interface_name);
    assert(out || out_size == 0);
    return snprintf(
        out,
        out_size,
        PREAMBLE_PREFIX "%d" PREAMBLE_DASH "%d" PREAMBLE_SUFFIX "\n"
                        "// One block per component; %s.compack ties each block name to its\n"
                        "// file id in the archive. `if3` selects the field set.\n\n",
        interface_id,
        component_count,
        interface_name);
}

char*
Editor_IfNewInterfaceText(
    const char* interface_name,
    int interface_id,
    size_t* out_length)
{
    static const char root[] = "[universe]\nif3=yes\ntype=0\nwidthmode=1\nheightmode=1\n"
                               "name=\ntargetverb=\n\n";
    int preamble;
    char* text;

    assert(interface_name);
    assert(out_length);

    preamble = Editor_IfEmitPreamble(interface_name, interface_id, 1, NULL, 0);
    assert(preamble > 0);
    text = ToriMem_Alloc(ToriMem_Editor(), (size_t)preamble + sizeof(root));
    assert(text);
    Editor_IfEmitPreamble(interface_name, interface_id, 1, text, (size_t)preamble + 1);
    memcpy(text + preamble, root, sizeof(root));
    *out_length = (size_t)preamble + sizeof(root) - 1;
    return text;
}

/* ---- a working text ------------------------------------------------------ */

struct if_work
{
    char* text;
    size_t length;
    size_t capacity;
};

static void
work_reserve(
    struct if_work* work,
    size_t length)
{
    if( length + 1 <= work->capacity )
        return;
    work->capacity = (length + 1) * 2;
    work->text = ToriMem_Realloc(ToriMem_Editor(), work->text, work->capacity);
    assert(work->text);
}

static void
work_set(
    struct if_work* work,
    const char* text,
    size_t length)
{
    work_reserve(work, length);
    if( length )
        memcpy(work->text, text, length);
    work->text[length] = '\0';
    work->length = length;
}

static void
work_splice(
    struct if_work* work,
    size_t offset,
    size_t remove,
    const char* insert,
    size_t insert_length)
{
    size_t next = work->length - remove + insert_length;

    assert(offset <= work->length);
    assert(remove <= work->length - offset);
    work_reserve(work, next);
    memmove(
        work->text + offset + insert_length,
        work->text + offset + remove,
        work->length - offset - remove + 1);
    if( insert_length )
        memcpy(work->text + offset, insert, insert_length);
    work->length = next;
}

static void
work_free(struct if_work* work)
{
    ToriMem_Free(ToriMem_Editor(), work->text);
    memset(work, 0, sizeof(*work));
}

/* ---- hooks --------------------------------------------------------------- */

/** cachepack's cp_unescape. */
static void
unescape(
    const char* value,
    char* out,
    size_t out_size)
{
    size_t w = 0;

    for( size_t i = 0; value[i] && w + 1 < out_size; i++ )
    {
        if( value[i] == '\\' && value[i + 1] )
        {
            char next = value[++i];
            out[w++] = next == 'n' ? '\n' : next == 'r' ? '\r' : next;
        }
        else
            out[w++] = value[i];
    }
    out[w] = '\0';
}

void
Editor_IfStringText(
    const char* raw,
    char* out,
    size_t out_size)
{
    assert(raw);
    assert(out);
    assert(out_size > 0);
    unescape(raw, out, out_size);
}

int
Editor_IfHookParse(
    const char* raw,
    struct Editor_IfHook* out)
{
    char buf[8192];
    char* cursor;

    assert(raw);
    assert(out);

    memset(out, 0, sizeof(*out));
    if( strlen(raw) >= sizeof(buf) )
        return 0;
    unescape(raw, buf, sizeof(buf));
    if( !buf[0] )
        return 1;

    cursor = buf;
    while( cursor )
    {
        /* split_script_var: the next unescaped comma, escapes collapsed. */
        char* read = cursor;
        char* write = cursor;
        char* next = NULL;
        struct Editor_IfHookArg* arg;

        while( *read )
        {
            if( *read == '\\' && read[1] )
            {
                *write++ = read[1];
                read += 2;
                continue;
            }
            if( *read == ',' )
            {
                next = read + 1;
                break;
            }
            *write++ = *read++;
        }
        *write = '\0';

        if( out->arg_count == EDITOR_IF_HOOK_ARGS_MAX )
            return 0;
        arg = &out->args[out->arg_count++];
        if( cursor[0] == 's' && cursor[1] == ':' )
        {
            if( strlen(cursor + 2) >= sizeof(arg->text) )
                return 0;
            arg->is_string = 1;
            snprintf(arg->text, sizeof(arg->text), "%s", cursor + 2);
        }
        else
        {
            const char* digits = (cursor[0] == 'i' && cursor[1] == ':') ? cursor + 2 : cursor;
            if( !parse_int(digits, &arg->value) )
                return 0;
        }
        cursor = next;
    }
    return 1;
}

/** Append `c` to `out` at `*w` when it fits; always counts. */
static void
put(
    char* out,
    size_t out_size,
    size_t* w,
    char c)
{
    if( *w + 1 < out_size )
        out[*w] = c;
    (*w)++;
}

/**
 * A logical string value as a raw line value: cachepack's cp_lines_add_str
 * escaping (markers, line breaks, backslashes, a leading '[') and then
 * line_escape's (`//` and trailing blanks). Returns bytes needed.
 */
size_t
Editor_IfStringRaw(
    const char* logical,
    char* out,
    size_t out_size)
{
    size_t length = strlen(logical);
    char* stage = ToriMem_Alloc(ToriMem_Editor(), length * 2 + 2);
    size_t s = 0;
    size_t w = 0;
    size_t trailing = 0;

    assert(logical);
    assert(out || out_size == 0);
    assert(stage);
    if( strcmp(logical, "default") == 0 || strcmp(logical, "empty") == 0 )
        stage[s++] = '\\';
    for( size_t i = 0; i < length; i++ )
    {
        unsigned char c = (unsigned char)logical[i];
        if( c == '\n' || c == '\r' || c == '\\' || (c == '[' && i == 0) )
        {
            stage[s++] = '\\';
            stage[s++] = c == '\n' ? 'n' : c == '\r' ? 'r' : (char)c;
        }
        else
            stage[s++] = (char)c;
    }
    stage[s] = '\0';

    while( trailing < s && stage[s - 1 - trailing] == ' ' && !text_escaped(stage, s - 1 - trailing) )
        trailing++;
    for( size_t i = 0; i < s; i++ )
    {
        char c = stage[i];
        if( c == '/' && i > 0 && stage[i - 1] == '/' && !text_escaped(stage, i - 1) )
            put(out, out_size, &w, '\\');
        else if( c == ' ' && i >= s - trailing )
            put(out, out_size, &w, '\\');
        put(out, out_size, &w, c);
    }
    if( out_size )
        out[w < out_size ? w : out_size - 1] = '\0';
    ToriMem_Free(ToriMem_Editor(), stage);
    return w;
}

size_t
Editor_IfHookFormat(
    const struct Editor_IfHook* hook,
    char* out,
    size_t out_size)
{
    size_t cap = 64;
    size_t w = 0;
    char* logical;
    size_t needed;

    assert(hook);
    assert(out || out_size == 0);

    for( int i = 0; i < hook->arg_count; i++ )
        cap += 16 + strlen(hook->args[i].text) * 2;
    logical = ToriMem_Alloc(ToriMem_Editor(), cap);
    assert(logical);
    for( int i = 0; i < hook->arg_count; i++ )
    {
        const struct Editor_IfHookArg* arg = &hook->args[i];
        if( arg->is_string )
        {
            w += (size_t)snprintf(logical + w, cap - w, i ? ",s:" : "s:");
            for( const char* scan = arg->text; *scan; scan++ )
            {
                if( *scan == ',' || *scan == '\\' )
                    logical[w++] = '\\';
                logical[w++] = *scan;
            }
            logical[w] = '\0';
        }
        else
            w += (size_t)snprintf(logical + w, cap - w, i ? ",i:%d" : "i:%d", arg->value);
    }
    logical[w] = '\0';
    needed = Editor_IfStringRaw(logical, out, out_size);
    ToriMem_Free(ToriMem_Editor(), logical);
    return needed;
}

static const struct Editor_IfEventArg k_event_args[] = {
    { -2147483647, "event_mousex" },
    { -2147483646, "event_mousey" },
    { -2147483645, "event_com" },
    { -2147483644, "event_opindex" },
    { -2147483643, "event_comsubid" },
    { -2147483642, "event_drop" },
    { -2147483641, "event_dropsubid" },
    { -2147483640, "event_keycode" },
    { -2147483639, "event_keychar" },
    { -2147483638, "event_opsubindex" },
};

int
Editor_IfEventArgCount(void)
{
    return (int)(sizeof(k_event_args) / sizeof(k_event_args[0]));
}

const struct Editor_IfEventArg*
Editor_IfEventArgAt(int i)
{
    assert(i >= 0);
    assert(i < Editor_IfEventArgCount());
    return &k_event_args[i];
}

const char*
Editor_IfEventArgName(int value)
{
    for( int i = 0; i < Editor_IfEventArgCount(); i++ )
    {
        if( k_event_args[i].value == value )
            return k_event_args[i].name;
    }
    return NULL;
}

/* ---- references and renumbering ------------------------------------------ */

enum remap_kind
{
    REMAP_INSERT = 0, /* child >= a -> +1 */
    REMAP_REMOVE,     /* child > a -> -1 (a itself must be unreferenced) */
    REMAP_MOVE,       /* a -> b, the run between shifts by one */
};

struct remap
{
    enum remap_kind kind;
    int a;
    int b;
};

static int
remap_child(
    const struct remap* map,
    int child)
{
    switch( map->kind )
    {
    case REMAP_INSERT:
        return child >= map->a ? child + 1 : child;
    case REMAP_REMOVE:
        return child > map->a ? child - 1 : child;
    case REMAP_MOVE:
        if( child == map->a )
            return map->b;
        if( map->a < map->b && child > map->a && child <= map->b )
            return child - 1;
        if( map->b < map->a && child >= map->b && child < map->a )
            return child + 1;
        return child;
    }
    return child;
}

/** Is a hook's int argument a full id of this interface's component? Only
 *  for interface > 0, where a full id cannot be mistaken for a small int. */
static int
hook_arg_child(
    int value,
    int interface_id,
    int component_count)
{
    if( interface_id <= 0 || value < 0 || (value >> 16) != interface_id )
        return -1;
    if( (value & 0xFFFF) >= component_count )
        return -1;
    return value & 0xFFFF;
}

/**
 * Rewrite every reference in block `block` through `map`. Returns 1 when a
 * line changed.
 */
static int
block_remap(
    struct Editor_CfgBlock* block,
    int interface_id,
    int component_count,
    const struct remap* map)
{
    int changed = 0;

    for( int line = 0; line < block->line_count; line++ )
    {
        const struct Editor_CfgLine* parsed = &block->lines[line];
        char key[EDITOR_CMD_NAME_MAX];
        char raw[8192];
        size_t value_length;
        const char* value;
        int row;

        if( parsed->kind != EDITOR_CFG_LINE_FIELD || parsed->part >= sizeof(key) )
            continue;
        snprintf(key, sizeof(key), "%.*s", (int)parsed->part, block->text + parsed->start);
        row = Editor_IfKeyLookup(key, NULL);
        if( row < 0 )
            continue;
        value = Editor_CfgBlockValue(block, line, &value_length);
        if( value_length >= sizeof(raw) )
            continue;
        snprintf(raw, sizeof(raw), "%.*s", (int)value_length, value);

        if( strcmp(key, "layer") == 0 )
        {
            int child = layer_child(raw, interface_id);
            int full = 0;
            char next[32];
            int mapped;

            if( child < 0 || !parse_int(raw, &full) )
                continue;
            mapped = remap_child(map, child);
            if( mapped == child )
                continue;
            /* The high half is the interface's, kept as the file wrote it. */
            snprintf(next, sizeof(next), "%d", (int)((unsigned)full & 0xFFFF0000u) | mapped);
            Editor_CfgBlockFieldSet(block, key, 0, next);
            changed = 1;
        }
        else if( strcmp(key, "linked") == 0 )
        {
            int child;
            int mapped;
            char next[32];

            if( !parse_int(raw, &child) || child < 0 )
                continue;
            mapped = remap_child(map, child);
            if( mapped == child )
                continue;
            snprintf(next, sizeof(next), "%d", mapped);
            Editor_CfgBlockFieldSet(block, key, 0, next);
            changed = 1;
        }
        else if( k_keys[row].kind == EDITOR_IF_VALUE_HOOK && interface_id > 0 )
        {
            struct Editor_IfHook* hook = ToriMem_Alloc(ToriMem_Editor(), sizeof(*hook));
            int touched = 0;

            assert(hook);
            if( Editor_IfHookParse(raw, hook) )
            {
                for( int i = 1; i < hook->arg_count; i++ )
                {
                    int child;
                    int mapped;

                    if( hook->args[i].is_string )
                        continue;
                    child = hook_arg_child(hook->args[i].value, interface_id, component_count);
                    if( child < 0 )
                        continue;
                    mapped = remap_child(map, child);
                    if( mapped == child )
                        continue;
                    hook->args[i].value = (interface_id << 16) | mapped;
                    touched = 1;
                }
            }
            if( touched )
            {
                char next[8192];
                if( Editor_IfHookFormat(hook, next, sizeof(next)) < sizeof(next) )
                {
                    Editor_CfgBlockFieldSet(block, key, 0, next);
                    changed = 1;
                }
            }
            ToriMem_Free(ToriMem_Editor(), hook);
        }
    }
    return changed;
}

/** Rewrite every block of `work` through `map`. */
static void
work_remap(
    struct if_work* work,
    int interface_id,
    int component_count,
    const struct remap* map)
{
    struct Editor_IfFile file;
    struct if_work next;

    memset(&next, 0, sizeof(next));
    Editor_IfIndex(&file, work->text, work->length);
    work_set(&next, work->text, file.index.preamble_end);
    for( int position = 0; position < file.index.block_count; position++ )
    {
        struct Editor_CfgBlock block;

        block_load(&file, position, &block);
        block_remap(&block, interface_id, component_count, map);
        work_splice(&next, next.length, 0, block.text, block.length);
        Editor_CfgBlockFree(&block);
    }
    Editor_IfFree(&file);
    work_free(work);
    *work = next;
}

/** Does anything in `file` other than block `self` refer to child `child`? */
static int
child_referenced(
    const struct Editor_IfFile* file,
    int child,
    int self)
{
    int referenced = 0;

    for( int position = 0; position < file->index.block_count && !referenced; position++ )
    {
        struct Editor_CfgBlock block;

        if( position == self )
            continue;
        block_load(file, position, &block);
        for( int line = 0; line < block.line_count && !referenced; line++ )
        {
            const struct Editor_CfgLine* parsed = &block.lines[line];
            char key[EDITOR_CMD_NAME_MAX];
            char raw[8192];
            size_t value_length;
            const char* value;
            int row;
            int number;

            if( parsed->kind != EDITOR_CFG_LINE_FIELD || parsed->part >= sizeof(key) )
                continue;
            snprintf(key, sizeof(key), "%.*s", (int)parsed->part, block.text + parsed->start);
            row = Editor_IfKeyLookup(key, NULL);
            if( row < 0 )
                continue;
            value = Editor_CfgBlockValue(&block, line, &value_length);
            if( value_length >= sizeof(raw) )
                continue;
            snprintf(raw, sizeof(raw), "%.*s", (int)value_length, value);
            if( strcmp(key, "layer") == 0 )
                referenced = layer_child(raw, file->interface_id) == child;
            else if( strcmp(key, "linked") == 0 )
                referenced = parse_int(raw, &number) && number == child;
            else if( k_keys[row].kind == EDITOR_IF_VALUE_HOOK && file->interface_id > 0 )
            {
                struct Editor_IfHook* hook = ToriMem_Alloc(ToriMem_Editor(), sizeof(*hook));
                assert(hook);
                if( Editor_IfHookParse(raw, hook) )
                {
                    for( int i = 1; i < hook->arg_count && !referenced; i++ )
                        referenced = !hook->args[i].is_string
                            && hook_arg_child(
                                   hook->args[i].value, file->interface_id,
                                   file->index.block_count)
                                == child;
                }
                ToriMem_Free(ToriMem_Editor(), hook);
            }
        }
        Editor_CfgBlockFree(&block);
    }
    return referenced;
}

/* ---- field edits --------------------------------------------------------- */

/** The line a new `key` goes at: after the last FIELD line that ranks below
 *  it, else right after the header. */
static int
canonical_line(
    const struct Editor_CfgBlock* block,
    int rank)
{
    int header = Editor_CfgBlockHeader(block);
    int insert = header + 1;

    for( int line = header + 1; line < block->line_count; line++ )
    {
        const struct Editor_CfgLine* parsed = &block->lines[line];
        char key[EDITOR_CMD_NAME_MAX];
        int line_rank;

        if( parsed->kind != EDITOR_CFG_LINE_FIELD )
            continue;
        if( parsed->part >= sizeof(key) )
            continue;
        snprintf(key, sizeof(key), "%.*s", (int)parsed->part, block->text + parsed->start);
        line_rank = Editor_IfKeyRank(key);
        if( line_rank >= 0 && line_rank < rank )
            insert = line + 1;
    }
    return insert;
}

/** A header name that survives as one: non-empty, printable, no ']'. */
static int
name_valid(const char* name)
{
    if( !name[0] || strlen(name) >= EDITOR_CMD_NAME_MAX )
        return 0;
    for( const char* c = name; *c; c++ )
    {
        if( (unsigned char)*c < 0x20 || *c == ']' || *c == '[' )
            return 0;
    }
    return 1;
}

/** Block `position`'s [start, end) in `work`, re-indexed. */
static int
work_block_extent(
    const struct if_work* work,
    int position,
    size_t* out_start,
    size_t* out_end,
    int* out_count,
    int* out_interface)
{
    struct Editor_IfFile file;
    int ok;

    Editor_IfIndex(&file, work->text, work->length);
    ok = position >= 0 && position < file.index.block_count;
    if( ok )
    {
        *out_start = file.index.blocks[position].start;
        *out_end = file.index.blocks[position].end;
    }
    if( out_count )
        *out_count = file.index.block_count;
    if( out_interface )
        *out_interface = file.interface_id;
    Editor_IfFree(&file);
    return ok;
}

/**
 * `key` of block `position` from `from` to `to` (NULL = no line; else the raw
 * value). "@name" renames. Returns 1 when the text changed.
 */
static int
work_field(
    struct if_work* work,
    int position,
    const char* key,
    const char* from,
    const char* to)
{
    struct Editor_CfgBlock block;
    size_t start;
    size_t end;
    int changed = 0;

    if( !work_block_extent(work, position, &start, &end, NULL, NULL) )
        return 0;
    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, work->text + start, end - start);

    if( strcmp(key, "@name") == 0 )
    {
        struct Editor_IfFile file;

        if( from && to && Editor_CfgBlockNameIs(&block, from) && name_valid(to) )
        {
            Editor_IfIndex(&file, work->text, work->length);
            if( Editor_IfFind(&file, to) < 0 )
                changed = Editor_CfgBlockRename(&block, to);
            Editor_IfFree(&file);
        }
    }
    else
    {
        char bare[EDITOR_CMD_NAME_MAX];
        int occurrence = 0;
        int line = -1;
        int rank = -1;
        int count = 0;

        /* `key@L`: the line a removal takes the field from, and so the line
         * its undo puts it back on — exact even in a hand-ordered block. */
        if( Editor_CfgFieldSpecParse(key, bare, sizeof(bare), &occurrence, &line)
            && occurrence == 0 )
        {
            rank = Editor_IfKeyRank(bare);
            count = Editor_CfgBlockFieldCount(&block, bare);
        }
        /* Only the grammar is enforced here — no line break — so an undo can
         * restore a value the tree holds that the emitter would not write
         * (a trailing blank). Editor_IfPlanField is where values are judged. */
        if( rank >= 0 && count <= 1 && (!to || (!strchr(to, '\n') && !strchr(to, '\r')))
            && (from ? count == 1 && Editor_CfgBlockFieldIs(&block, bare, 0, from) : count == 0) )
        {
            if( !to && from )
                changed = Editor_CfgBlockFieldRemove(&block, bare, 0, line);
            else if( to && !from )
                changed = Editor_CfgBlockFieldInsert(
                    &block, bare, 0, line >= 0 ? line : canonical_line(&block, rank), to);
            else if( to && from )
                changed = line < 0 && strcmp(from, to) != 0
                    && Editor_CfgBlockFieldSet(&block, bare, 0, to);
        }
    }
    if( changed )
        work_splice(work, start, end - start, block.text, block.length);
    Editor_CfgBlockFree(&block);
    return changed;
}

/** "=<raw>" -> raw, "" -> NULL (no line). */
static const char*
field_side(const char* value)
{
    return value[0] == '=' ? value + 1 : NULL;
}

/* ---- structure edits ----------------------------------------------------- */

void
Editor_IfFreshName(
    const struct Editor_IfFile* file,
    int position,
    int skip,
    char* out,
    size_t out_size)
{
    assert(file);
    assert(out);

    for( int attempt = 1;; attempt++ )
    {
        int taken = 0;

        if( attempt == 1 )
            snprintf(out, out_size, "com_%d", position);
        else
            snprintf(out, out_size, "com_%d_%d", position, attempt);
        for( int i = 0; i < file->index.block_count && !taken; i++ )
        {
            const struct Editor_CfgBlockRef* ref = &file->index.blocks[i];
            if( i == skip )
                continue;
            taken = ref->name_length == strlen(out)
                && memcmp(file->text + ref->name_start, out, ref->name_length) == 0;
        }
        if( !taken )
            return;
    }
}

/** The fresh block text (header through its blank separator). */
static int
fresh_text(
    const char* name,
    int interface_id,
    int parent,
    char* out,
    size_t out_size)
{
    if( parent >= 0 )
        return snprintf(
            out, out_size, "[%s]\nif3=yes\ntype=0\nlayer=%d\nname=\ntargetverb=\n\n", name,
            (interface_id > 0 ? interface_id << 16 : 0) | parent);
    return snprintf(out, out_size, "[%s]\nif3=yes\ntype=0\nname=\ntargetverb=\n\n", name);
}

/** The preamble's count, moved by `delta`, when the preamble is cachepack's. */
static void
work_count(
    struct if_work* work,
    int delta)
{
    int count;
    size_t at;
    size_t end;
    char digits[16];

    if( !preamble_parse(work->text, work->length, NULL, &count, &at, &end) )
        return;
    snprintf(digits, sizeof(digits), "%d", count + delta);
    work_splice(work, at, end - at, digits, strlen(digits));
}

/** 1 when the text ends with a blank separator line ("\n\n"). */
static int
ends_blank(const struct if_work* work)
{
    return work->length >= 2 && work->text[work->length - 1] == '\n'
        && work->text[work->length - 2] == '\n';
}

static int
work_add(
    struct if_work* work,
    int position,
    int parent)
{
    struct Editor_IfFile file;
    char name[EDITOR_CMD_NAME_MAX];
    char fresh[512];
    int fresh_length;
    int count;
    int interface_id;
    struct remap map = { REMAP_INSERT, position, 0 };

    Editor_IfIndex(&file, work->text, work->length);
    count = file.index.block_count;
    interface_id = file.interface_id;
    if( position < 0 || position > count || parent < -1 || parent > count || parent == position
        || work->length == 0 || work->text[work->length - 1] != '\n' )
    {
        Editor_IfFree(&file);
        return 0;
    }
    Editor_IfFreshName(&file, position, -1, name, sizeof(name));
    Editor_IfFree(&file);

    work_remap(work, interface_id, count, &map);
    fresh_length = fresh_text(name, interface_id, parent, fresh, sizeof(fresh));
    assert(fresh_length > 0 && (size_t)fresh_length < sizeof(fresh));
    if( position < count )
    {
        size_t start = 0;
        size_t end = 0;
        int ok = work_block_extent(work, position, &start, &end, NULL, NULL);
        assert(ok);
        (void)ok;
        work_splice(work, start, 0, fresh, (size_t)fresh_length);
    }
    else if( ends_blank(work) || count == 0 )
        work_splice(work, work->length, 0, fresh, (size_t)fresh_length);
    else
    {
        /* The last block has no blank line after it: keep that shape, the
         * separator goes before the new block. */
        work_splice(work, work->length, 0, "\n", 1);
        work_splice(work, work->length, 0, fresh, (size_t)fresh_length - 1);
    }
    work_count(work, 1);
    return 1;
}

static int
work_remove(
    struct if_work* work,
    int position,
    int parent)
{
    struct Editor_IfFile file;
    char name[EDITOR_CMD_NAME_MAX];
    char fresh[512];
    int fresh_length;
    int count;
    int interface_id;
    size_t start;
    size_t end;
    struct remap map = { REMAP_REMOVE, position, 0 };

    Editor_IfIndex(&file, work->text, work->length);
    count = file.index.block_count;
    interface_id = file.interface_id;
    if( position < 0 || position >= count || child_referenced(&file, position, position) )
    {
        Editor_IfFree(&file);
        return 0;
    }
    Editor_IfFreshName(&file, position, position, name, sizeof(name));
    start = file.index.blocks[position].start;
    end = file.index.blocks[position].end;
    Editor_IfFree(&file);

    fresh_length = fresh_text(name, interface_id, parent, fresh, sizeof(fresh));
    assert(fresh_length > 0 && (size_t)fresh_length < sizeof(fresh));
    if( end - start == (size_t)fresh_length && memcmp(work->text + start, fresh, end - start) == 0 )
        work_splice(work, start, end - start, NULL, 0);
    else if(
        position == count - 1 && end == work->length && end - start == (size_t)fresh_length - 1
        && memcmp(work->text + start, fresh, end - start) == 0 && start >= 2
        && work->text[start - 1] == '\n' && work->text[start - 2] == '\n' )
        work_splice(work, start - 1, end - start + 1, NULL, 0);
    else
        return 0;
    work_remap(work, interface_id, count, &map);
    work_count(work, -1);
    return 1;
}

static int
work_move(
    struct if_work* work,
    int from,
    int to)
{
    struct Editor_IfFile file;
    struct if_work next;
    int count;
    int interface_id;
    int* order;
    struct remap map = { REMAP_MOVE, from, to };
    int at;

    if( from == to )
        return 1;
    Editor_IfIndex(&file, work->text, work->length);
    count = file.index.block_count;
    interface_id = file.interface_id;
    if( from < 0 || from >= count || to < 0 || to >= count || !ends_blank(work) )
    {
        Editor_IfFree(&file);
        return 0;
    }

    order = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)count);
    assert(order);
    at = 0;
    for( int i = 0; i < count; i++ )
    {
        if( i == from )
            continue;
        if( at == to )
            order[at++] = from;
        order[at++] = i;
    }
    if( at == to )
        order[at++] = from;
    assert(at == count);

    memset(&next, 0, sizeof(next));
    work_set(&next, work->text, file.index.preamble_end);
    for( int i = 0; i < count; i++ )
    {
        const struct Editor_CfgBlockRef* ref = &file.index.blocks[order[i]];
        work_splice(&next, next.length, 0, work->text + ref->start, ref->end - ref->start);
    }
    ToriMem_Free(ToriMem_Editor(), order);
    Editor_IfFree(&file);
    work_free(work);
    *work = next;
    work_remap(work, interface_id, count, &map);
    return 1;
}

/** Would `child`'s parent chain, with `position` reparented to `parent`,
 *  come back to `position`? */
static int
reparent_cycles(
    const struct Editor_IfFile* file,
    int position,
    int parent)
{
    int steps = 0;
    int at = parent;

    while( at >= 0 && steps <= file->index.block_count )
    {
        if( at == position )
            return 1;
        if( at >= file->index.block_count )
            return 0;
        at = Editor_IfBlockParent(file, at);
        steps++;
    }
    return steps > file->index.block_count;
}

/** `layer=` of block `position` from parent `from` to `to` (child indices,
 *  -1 = none), in the current numbering. */
static int
work_reparent(
    struct if_work* work,
    int position,
    int from,
    int to)
{
    struct Editor_IfFile file;
    char before[32];
    char after[32];
    int ok;

    if( from == to )
        return 1;
    Editor_IfIndex(&file, work->text, work->length);
    ok = position >= 0 && position < file.index.block_count
        && Editor_IfBlockParent(&file, position) == from && to < file.index.block_count
        && to >= -1 && !reparent_cycles(&file, position, to);
    if( ok )
    {
        int base = file.interface_id > 0 ? file.interface_id << 16 : 0;
        snprintf(before, sizeof(before), "%d", base | from);
        snprintf(after, sizeof(after), "%d", base | to);
    }
    Editor_IfFree(&file);
    if( !ok )
        return 0;
    return work_field(work, position, "layer", from >= 0 ? before : NULL, to >= 0 ? after : NULL);
}

/* ---- commands ------------------------------------------------------------ */

static int
edit_component(
    struct if_work* work,
    const struct Editor_Cmd* command,
    int forward)
{
    const int index_before = command->u.if_component.index_before;
    const int index_after = command->u.if_component.index_after;
    const int parent_before = command->u.if_component.parent_before;
    const int parent_after = command->u.if_component.parent_after;

    {
        struct Editor_IfFile file;
        int interface_id;

        Editor_IfIndex(&file, work->text, work->length);
        interface_id = file.interface_id;
        Editor_IfFree(&file);
        /* Without the preamble's id a full component id cannot be built. */
        if( interface_id < 0 )
            return 0;
    }
    if( command->has_after && !command->has_before )
        return forward ? work_add(work, index_after, parent_after)
                       : work_remove(work, index_after, parent_after);
    if( command->has_before && !command->has_after )
        return forward ? work_remove(work, index_before, parent_before)
                       : work_add(work, index_before, parent_before);
    if( !command->has_before || !command->has_after )
        return 0;
    if( forward )
        return work_reparent(work, index_before, parent_before, parent_after)
            && work_move(work, index_before, index_after);
    return work_move(work, index_after, index_before)
        && work_reparent(work, index_before, parent_after, parent_before);
}

/** A string field of a command that is terminated inside its array. */
static int
terminated(
    const char* field,
    size_t size)
{
    return memchr(field, '\0', size) != NULL;
}

char*
Editor_IfEdit(
    const char* text,
    size_t length,
    const struct Editor_Cmd* command,
    int forward,
    size_t* out_length)
{
    struct if_work work;
    int changed = 0;

    assert(text || length == 0);
    assert(command);
    assert(out_length);

    memset(&work, 0, sizeof(work));
    work_set(&work, text, length);
    if( command->kind == EDITOR_CMD_IF_FIELD )
    {
        const char* before = command->u.if_field.before;
        const char* after = command->u.if_field.after;

        if( terminated(command->u.if_field.key, sizeof(command->u.if_field.key))
            && terminated(before, sizeof(command->u.if_field.before))
            && terminated(after, sizeof(command->u.if_field.after))
            && (before[0] == '\0' || before[0] == '=') && (after[0] == '\0' || after[0] == '=') )
        {
            changed = work_field(
                &work, command->u.if_field.component, command->u.if_field.key,
                field_side(forward ? before : after), field_side(forward ? after : before));
        }
    }
    else if( command->kind == EDITOR_CMD_IF_COMPONENT )
    {
        /* A failed half of a two-part edit leaves `work` partly edited; it is
         * discarded with the rest. */
        changed = edit_component(&work, command, forward);
    }
    if( !changed || (work.length == length && memcmp(work.text, text, length) == 0) )
    {
        work_free(&work);
        return NULL;
    }
    *out_length = work.length;
    return work.text;
}

int
Editor_IfDocApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct Editor_DocHandle handle;
    char* next;
    size_t next_length = 0;

    assert(doc);
    assert(command);

    if( command->doc_kind != EDITOR_DOC_IFACE
        || (command->kind != EDITOR_CMD_IF_FIELD && command->kind != EDITOR_CMD_IF_COMPONENT)
        || !terminated(command->doc_key, sizeof(command->doc_key))
        || !Editor_DocFind(doc, EDITOR_DOC_IFACE, command->doc_key, &handle) || !handle.text )
        return 0;
    next = Editor_IfEdit(
        handle.text->text ? handle.text->text : "", handle.text->length, command,
        direction == EDITOR_CMD_FORWARD, &next_length);
    if( !next )
        return 0;
    Editor_TextDocSplice(handle.text, 0, handle.text->length, next, next_length);
    handle.text->dirty = 1;
    ToriMem_Free(ToriMem_Editor(), next);
    return 1;
}

/* ---- plans --------------------------------------------------------------- */

int
Editor_IfPlanField(
    const char* text,
    size_t length,
    const char* doc_key,
    int position,
    const char* key,
    const char* raw,
    struct Editor_Cmd* out)
{
    struct Editor_IfFile file;
    char current[EDITOR_CMD_VALUE_MAX];
    int found;

    assert(text || length == 0);
    assert(doc_key);
    assert(key);
    assert(out);

    if( strlen(key) >= EDITOR_CMD_NAME_MAX || (strchr(key, '@') && strcmp(key, "@name") != 0) )
        return 0;
    if( raw && strlen(raw) + 2 > EDITOR_CMD_VALUE_MAX )
        return 0;
    Editor_IfIndex(&file, text, length);
    if( position < 0 || position >= file.index.block_count )
    {
        Editor_IfFree(&file);
        return 0;
    }
    if( strcmp(key, "@name") == 0 )
    {
        Editor_IfBlockName(&file, position, current, sizeof(current));
        found = 1;
        if( !raw || !name_valid(raw) || strcmp(raw, current) == 0 || Editor_IfFind(&file, raw) >= 0 )
        {
            Editor_IfFree(&file);
            return 0;
        }
    }
    else
    {
        found = Editor_IfBlockValue(&file, position, key, current, sizeof(current) - 1);
        if( found == 2 || Editor_IfKeyRank(key) < 0 || (raw && !Editor_IfValueValid(key, raw))
            || (!found && !raw) || (found && raw && strcmp(current, raw) == 0) )
        {
            Editor_IfFree(&file);
            return 0;
        }
    }
    Editor_IfFree(&file);

    Editor_CmdInit(out, EDITOR_DOC_IFACE, doc_key, EDITOR_CMD_IF_FIELD);
    out->has_before = 1;
    out->has_after = 1;
    out->u.if_field.component = position;
    snprintf(out->u.if_field.key, sizeof(out->u.if_field.key), "%s", key);
    if( found && !raw )
    {
        /* A removal names its line, so its undo is exact anywhere. */
        struct Editor_CfgBlock block;
        int line;

        Editor_IfIndex(&file, text, length);
        block_load(&file, position, &block);
        Editor_IfFree(&file);
        line = Editor_CfgBlockFieldFind(&block, key, 0);
        Editor_CfgBlockFree(&block);
        if( line < 0
            || !Editor_CfgFieldSpecFormat(key, 0, line, out->u.if_field.key, sizeof(out->u.if_field.key)) )
            return 0;
    }
    if( found )
        snprintf(out->u.if_field.before, sizeof(out->u.if_field.before), "=%s", current);
    if( raw )
        snprintf(out->u.if_field.after, sizeof(out->u.if_field.after), "=%s", raw);
    return 1;
}

/** Lines a fresh block keeps (with these values). */
static int
fresh_keeps(
    const char* key,
    const char* value)
{
    if( strcmp(key, "if3") == 0 )
        return strcmp(value, "yes") == 0;
    if( strcmp(key, "type") == 0 )
        return strcmp(value, "0") == 0;
    if( strcmp(key, "name") == 0 || strcmp(key, "targetverb") == 0 )
        return value[0] == '\0';
    return strcmp(key, "layer") == 0;
}

/** Append `command` to the plan and apply it to `work`. */
static int
plan_push(
    struct if_work* work,
    const struct Editor_Cmd* command,
    struct Editor_Cmd* out,
    int max,
    int* count)
{
    char* next;
    size_t next_length = 0;

    if( *count >= max )
        return 0;
    next = Editor_IfEdit(work->text, work->length, command, 1, &next_length);
    if( !next )
        return 0;
    work_set(work, next, next_length);
    ToriMem_Free(ToriMem_Editor(), next);
    out[(*count)++] = *command;
    return 1;
}

/** IF_FIELDs that bring block `position` back to a fresh block's lines
 *  (its name and `layer=` stay). */
static int
plan_clear(
    struct if_work* work,
    const char* doc_key,
    int position,
    struct Editor_Cmd* out,
    int max,
    int* count)
{
    static const char* const ensure[] = { "if3", "type", "name", "targetverb" };
    static const char* const ensure_value[] = { "yes", "0", "", "" };
    struct Editor_IfFile file;
    struct Editor_CfgBlock block;
    struct Editor_Cmd command;
    int ok = 1;

    /* Every line a fresh block does not keep, last first. */
    Editor_IfIndex(&file, work->text, work->length);
    block_load(&file, position, &block);
    Editor_IfFree(&file);
    for( int line = block.line_count - 1; line >= 0 && ok; line-- )
    {
        const struct Editor_CfgLine* parsed = &block.lines[line];
        char key[EDITOR_CMD_NAME_MAX];
        char value[EDITOR_CMD_VALUE_MAX];
        size_t value_length;
        const char* raw;

        if( parsed->kind != EDITOR_CFG_LINE_FIELD )
            continue;
        if( parsed->part >= sizeof(key) )
        {
            ok = 0;
            break;
        }
        snprintf(key, sizeof(key), "%.*s", (int)parsed->part, block.text + parsed->start);
        raw = Editor_CfgBlockValue(&block, line, &value_length);
        if( value_length + 2 > sizeof(value) )
        {
            ok = 0;
            break;
        }
        snprintf(value, sizeof(value), "%.*s", (int)value_length, raw);
        if( fresh_keeps(key, value) )
            continue;
        Editor_CmdInit(&command, EDITOR_DOC_IFACE, doc_key, EDITOR_CMD_IF_FIELD);
        command.has_before = 1;
        command.has_after = 1;
        command.u.if_field.component = position;
        if( !Editor_CfgFieldSpecFormat(
                key, 0, line, command.u.if_field.key, sizeof(command.u.if_field.key)) )
        {
            ok = 0;
            break;
        }
        snprintf(command.u.if_field.before, sizeof(command.u.if_field.before), "=%s", value);
        ok = plan_push(work, &command, out, max, count);
    }
    Editor_CfgBlockFree(&block);

    for( int i = 0; i < 4 && ok; i++ )
    {
        char current[16];
        int found;

        Editor_IfIndex(&file, work->text, work->length);
        found = Editor_IfBlockValue(&file, position, ensure[i], current, sizeof(current));
        Editor_IfFree(&file);
        if( found == 1 && strcmp(current, ensure_value[i]) == 0 )
            continue;
        ok = Editor_IfPlanField(
                 work->text, work->length, doc_key, position, ensure[i], ensure_value[i], &command)
            && plan_push(work, &command, out, max, count);
    }
    return ok;
}

/** Rename a cleared block `position` to its fresh name and remove it. */
static int
plan_drop(
    struct if_work* work,
    const char* doc_key,
    int position,
    struct Editor_Cmd* out,
    int max,
    int* count)
{
    struct Editor_IfFile file;
    struct Editor_Cmd command;
    char name[EDITOR_CMD_NAME_MAX];
    char fresh[EDITOR_CMD_NAME_MAX];
    int parent;
    int ok = 1;

    Editor_IfIndex(&file, work->text, work->length);
    Editor_IfBlockName(&file, position, name, sizeof(name));
    Editor_IfFreshName(&file, position, position, fresh, sizeof(fresh));
    parent = Editor_IfBlockParent(&file, position);
    Editor_IfFree(&file);
    if( parent < -1 )
        return 0;
    if( strcmp(name, fresh) != 0 )
        ok = Editor_IfPlanField(work->text, work->length, doc_key, position, "@name", fresh, &command)
            && plan_push(work, &command, out, max, count);
    if( !ok )
        return 0;
    Editor_CmdInit(&command, EDITOR_DOC_IFACE, doc_key, EDITOR_CMD_IF_COMPONENT);
    command.has_before = 1;
    command.u.if_component.component = position;
    command.u.if_component.index_before = position;
    command.u.if_component.index_after = -1;
    command.u.if_component.parent_before = parent;
    command.u.if_component.parent_after = -1;
    return plan_push(work, &command, out, max, count);
}

int
Editor_IfPlanDelete(
    const char* text,
    size_t length,
    const char* doc_key,
    int position,
    struct Editor_Cmd* out,
    int max)
{
    struct if_work work;
    struct Editor_IfFile file;
    int count = 0;
    int ok = 1;
    char (*names)[EDITOR_CMD_NAME_MAX] = NULL;
    int name_count = 0;

    assert(text || length == 0);
    assert(doc_key);
    assert(out || max == 0);

    /* The subtree, by name: positions shift as the stroke removes blocks. */
    Editor_IfIndex(&file, text, length);
    if( position < 0 || position >= file.index.block_count )
    {
        Editor_IfFree(&file);
        return -1;
    }
    names = ToriMem_Alloc(ToriMem_Editor(), sizeof(*names) * (size_t)file.index.block_count);
    assert(names);
    {
        unsigned char* in = ToriMem_Calloc(ToriMem_Editor(), (size_t)file.index.block_count, 1);
        int grew = 1;

        assert(in);
        in[position] = 1;
        while( grew )
        {
            grew = 0;
            for( int i = 0; i < file.index.block_count; i++ )
            {
                int parent = Editor_IfBlockParent(&file, i);
                if( !in[i] && parent >= 0 && parent < file.index.block_count && in[parent] )
                {
                    in[i] = 1;
                    grew = 1;
                }
            }
        }
        for( int i = 0; i < file.index.block_count; i++ )
        {
            if( in[i] )
                Editor_IfBlockName(&file, i, names[name_count++], EDITOR_CMD_NAME_MAX);
        }
        ToriMem_Free(ToriMem_Editor(), in);
    }
    Editor_IfFree(&file);

    memset(&work, 0, sizeof(work));
    work_set(&work, text, length);
    /* Clear every member first: a member's hook may name another member,
     * and nothing referenced can be removed. */
    Editor_IfIndex(&file, work.text, work.length);
    {
        int* positions = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)(name_count + 1));
        assert(positions);
        for( int n = 0; n < name_count; n++ )
            positions[n] = Editor_IfFind(&file, names[n]);
        Editor_IfFree(&file);
        for( int n = 0; n < name_count && ok; n++ )
            ok = positions[n] >= 0 && plan_clear(&work, doc_key, positions[n], out, max, &count);
        ToriMem_Free(ToriMem_Editor(), positions);
    }
    /* Then a leaf at a time, the highest position first. */
    for( int done = 0; done < name_count && ok; done++ )
    {
        int pick = -1;

        int* parents;
        int pick_at = -1;

        Editor_IfIndex(&file, work.text, work.length);
        parents = ToriMem_Alloc(ToriMem_Editor(), sizeof(int) * (size_t)(file.index.block_count + 1));
        assert(parents);
        for( int i = 0; i < file.index.block_count; i++ )
            parents[i] = Editor_IfBlockParent(&file, i);
        for( int n = 0; n < name_count; n++ )
        {
            int at = names[n][0] ? Editor_IfFind(&file, names[n]) : -1;
            int has_child = 0;

            if( at < 0 )
                continue;
            for( int i = 0; i < file.index.block_count && !has_child; i++ )
                has_child = parents[i] == at;
            if( !has_child && at > pick_at )
            {
                pick = n;
                pick_at = at;
            }
        }
        ToriMem_Free(ToriMem_Editor(), parents);
        if( pick < 0 )
            ok = 0;
        else
        {
            Editor_IfFree(&file);
            names[pick][0] = '\0';
            ok = plan_drop(&work, doc_key, pick_at, out, max, &count);
            continue;
        }
        Editor_IfFree(&file);
    }
    ToriMem_Free(ToriMem_Editor(), names);
    work_free(&work);
    return ok ? count : -1;
}

/* ---- the compack --------------------------------------------------------- */

/** Next compack line at `*at`: 1 for an `id=name` entry (filled), 0 for any
 *  other line; -1 at the end. */
static int
compack_line(
    const char* text,
    size_t length,
    size_t* at,
    size_t* out_start,
    size_t* out_end,
    long* out_id,
    const char** out_name,
    size_t* out_name_length)
{
    size_t start = *at;
    const char* newline;
    size_t stop;
    size_t content;
    size_t i;
    long id = 0;

    if( start >= length )
        return -1;
    newline = memchr(text + start, '\n', length - start);
    stop = newline ? (size_t)(newline - text) : length;
    *at = newline ? stop + 1 : length;
    *out_start = start;
    *out_end = *at;
    content = stop;
    if( content > start && text[content - 1] == '\r' )
        content--;
    i = start;
    while( i < content && text[i] >= '0' && text[i] <= '9' )
        id = id * 10 + (text[i++] - '0');
    if( i == start || i >= content || text[i] != '=' )
        return 0;
    *out_id = id;
    *out_name = text + i + 1;
    *out_name_length = content - i - 1;
    return 1;
}

int
Editor_IfCompackPositional(
    const char* compack,
    size_t compack_length,
    const char* if_text,
    size_t if_length)
{
    struct Editor_IfFile file;
    size_t at = 0;
    int entries = 0;
    int ok = 1;
    int kind;
    size_t start;
    size_t end;
    long id;
    const char* name;
    size_t name_length;

    assert(compack || compack_length == 0);
    assert(if_text || if_length == 0);

    Editor_IfIndex(&file, if_text, if_length);
    while( ok
           && (kind = compack_line(
                   compack, compack_length, &at, &start, &end, &id, &name, &name_length))
               >= 0 )
    {
        const struct Editor_CfgBlockRef* ref;

        if( kind == 0 )
            continue;
        if( id != entries || entries >= file.index.block_count )
        {
            ok = 0;
            break;
        }
        ref = &file.index.blocks[entries];
        ok = ref->name_length == name_length
            && memcmp(if_text + ref->name_start, name, name_length) == 0;
        entries++;
    }
    ok = ok && entries == file.index.block_count;
    Editor_IfFree(&file);
    return ok;
}

/** The two texts name the same blocks in the same order. */
static int
same_names(
    const char* a,
    size_t a_length,
    const char* b,
    size_t b_length)
{
    struct Editor_IfFile fa;
    struct Editor_IfFile fb;
    int same;

    Editor_IfIndex(&fa, a, a_length);
    Editor_IfIndex(&fb, b, b_length);
    same = fa.index.block_count == fb.index.block_count;
    for( int i = 0; same && i < fa.index.block_count; i++ )
    {
        const struct Editor_CfgBlockRef* ra = &fa.index.blocks[i];
        const struct Editor_CfgBlockRef* rb = &fb.index.blocks[i];
        same = ra->name_length == rb->name_length
            && memcmp(a + ra->name_start, b + rb->name_start, ra->name_length) == 0;
    }
    Editor_IfFree(&fa);
    Editor_IfFree(&fb);
    return same;
}

int
Editor_IfCompackForSave(
    const char* compack,
    size_t compack_length,
    const char* old_if,
    size_t old_if_length,
    const char* new_if,
    size_t new_if_length,
    char** out,
    size_t* out_length)
{
    struct Editor_IfFile file;
    struct if_work work;
    size_t at = 0;
    int entries = 0;
    int kind;
    size_t start;
    size_t end;
    long id;
    const char* name;
    size_t name_length;

    assert(compack || compack_length == 0);
    assert(old_if || old_if_length == 0);
    assert(new_if || new_if_length == 0);
    assert(out);
    assert(out_length);

    *out = NULL;
    *out_length = 0;
    if( same_names(old_if, old_if_length, new_if, new_if_length) )
        return 1;
    if( !Editor_IfCompackPositional(compack, compack_length, old_if, old_if_length) )
        return 0;

    memset(&work, 0, sizeof(work));
    work_set(&work, "", 0);
    Editor_IfIndex(&file, new_if, new_if_length);
    while( (kind = compack_line(
                compack, compack_length, &at, &start, &end, &id, &name, &name_length))
           >= 0 )
    {
        if( kind == 0 )
        {
            work_splice(&work, work.length, 0, compack + start, end - start);
            continue;
        }
        if( entries < file.index.block_count )
        {
            char line[EDITOR_CMD_NAME_MAX + 32];
            char block_name[EDITOR_CMD_NAME_MAX];
            int written;

            Editor_IfBlockName(&file, entries, block_name, sizeof(block_name));
            written = snprintf(line, sizeof(line), "%d=%s\n", entries, block_name);
            work_splice(&work, work.length, 0, line, (size_t)written);
        }
        entries++;
    }
    if( work.length > 0 && work.text[work.length - 1] != '\n' )
        work_splice(&work, work.length, 0, "\n", 1);
    for( ; entries < file.index.block_count; entries++ )
    {
        char line[EDITOR_CMD_NAME_MAX + 32];
        char block_name[EDITOR_CMD_NAME_MAX];
        int written;

        Editor_IfBlockName(&file, entries, block_name, sizeof(block_name));
        written = snprintf(line, sizeof(line), "%d=%s\n", entries, block_name);
        work_splice(&work, work.length, 0, line, (size_t)written);
    }
    Editor_IfFree(&file);
    *out = work.text;
    *out_length = work.length;
    return 1;
}
