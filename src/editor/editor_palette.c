/**
 * The PALETTES codec. See editor_palette.h for the file, its grammar and why
 * it keeps lines rather than records.
 */

#include "editor_palette.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What an empty file gains with its first record (and loses with its last). */
static const char* const k_header[] = {
    "# ToriRSMapEd catalog palettes (editor/palettes.txt).",
    "# palette <name> [| note <text>]",
    "# palette <name> | slot <1..10> | <loc|npc|obj>:<record> [| shape <0..22> rot <0..3>] [| note <text>]",
};
#define PALETTES_HEADER_LINES ((int)(sizeof(k_header) / sizeof(k_header[0])))

static const char* const k_kind_names[3] = { "loc", "npc", "obj" };

/* ---- lines ------------------------------------------------------------------ */

void
Editor_PalettesInit(struct Editor_PalettesFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

static void
line_free(struct Editor_PalettesLine* line)
{
    ToriMem_Free(ToriMem_Editor(), line->text);
    ToriMem_Free(ToriMem_Editor(), line->row);
    memset(line, 0, sizeof(*line));
}

void
Editor_PalettesFree(struct Editor_PalettesFile* file)
{
    if( !file )
        return;
    for( int i = 0; i < file->line_count; i++ )
        line_free(&file->lines[i]);
    ToriMem_Free(ToriMem_Editor(), file->lines);
    memset(file, 0, sizeof(*file));
}

static char*
copy_text(
    const char* text,
    size_t length)
{
    char* out = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(out);
    if( length > 0 )
        memcpy(out, text, length);
    out[length] = '\0';
    return out;
}

static struct Editor_PalettesLine*
insert_slot(
    struct Editor_PalettesFile* file,
    int index)
{
    assert(index >= 0);
    assert(index <= file->line_count);

    if( file->line_count == file->line_capacity )
    {
        int next = file->line_capacity ? file->line_capacity * 2 : 32;
        file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, (size_t)next * sizeof(*file->lines));
        assert(file->lines);
        file->line_capacity = next;
    }
    memmove(&file->lines[index + 1], &file->lines[index],
        (size_t)(file->line_count - index) * sizeof(*file->lines));
    file->line_count++;
    memset(&file->lines[index], 0, sizeof(file->lines[index]));
    return &file->lines[index];
}

static void
remove_slot(
    struct Editor_PalettesFile* file,
    int index)
{
    assert(index >= 0);
    assert(index < file->line_count);
    line_free(&file->lines[index]);
    memmove(&file->lines[index], &file->lines[index + 1],
        (size_t)(file->line_count - index - 1) * sizeof(*file->lines));
    file->line_count--;
}

/* ---- one record line ----------------------------------------------------------- */

static int
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static void
trim(
    const char** begin,
    const char** end)
{
    while( *begin < *end && is_space(**begin) )
        (*begin)++;
    while( *end > *begin && is_space((*end)[-1]) )
        (*end)--;
}

static int
printable_run(
    const char* begin,
    const char* end)
{
    for( const char* at = begin; at < end; at++ )
    {
        unsigned char const c = (unsigned char)*at;
        if( c < 0x20 || c == 0x7f || c == '|' )
            return 0;
    }
    return 1;
}

int
Editor_PaletteNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_PALETTE_NAME_MAX )
        return 0;
    if( is_space(name[0]) || is_space(name[length - 1]) )
        return 0;
    if( name[0] == '#' || (name[0] == '/' && name[1] == '/') )
        return 0;
    return printable_run(name, name + length);
}

static int
note_valid(const char* note)
{
    size_t const length = strlen(note);
    return length < EDITOR_PALETTE_NOTE_MAX && printable_run(note, note + length);
}

int
Editor_PaletteRecordValid(const char* record)
{
    size_t length;

    assert(record);
    length = strlen(record);
    if( length == 0 || length >= EDITOR_PALETTE_RECORD_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        unsigned char const c = (unsigned char)record[i];
        if( c <= 0x20 || c == 0x7f || c == '|' || c == ':' )
            return 0;
    }
    return 1;
}

const char*
Editor_PaletteKindName(int kind)
{
    return kind >= 0 && kind < 3 ? k_kind_names[kind] : "?";
}

int
Editor_PaletteKindFromName(const char* name)
{
    assert(name);
    for( int i = 0; i < 3; i++ )
        if( strcmp(name, k_kind_names[i]) == 0 )
            return i;
    return -1;
}

void
Editor_PaletteHeaderRow(
    struct Editor_PaletteRow* out,
    const char* name,
    const char* note)
{
    assert(out);
    assert(name);
    memset(out, 0, sizeof(*out));
    snprintf(out->palette, sizeof(out->palette), "%s", name);
    out->slot = 0;
    out->kind = -1;
    out->shape = -1;
    if( note )
        snprintf(out->note, sizeof(out->note), "%s", note);
}

void
Editor_PaletteEntryRow(
    struct Editor_PaletteRow* out,
    const char* name,
    int slot,
    int kind,
    const char* record)
{
    assert(out);
    assert(name);
    assert(record);
    memset(out, 0, sizeof(*out));
    snprintf(out->palette, sizeof(out->palette), "%s", name);
    out->slot = slot;
    out->kind = kind;
    snprintf(out->record, sizeof(out->record), "%s", record);
    out->shape = -1;
}

int
Editor_PaletteFormat(
    const struct Editor_PaletteRow* row,
    char* out,
    size_t out_capacity)
{
    char buffer[EDITOR_CMD_SPLICE_MAX];
    int at;

    assert(row);
    assert(out || out_capacity == 0);

    at = snprintf(buffer, sizeof(buffer), "palette %s", row->palette);
    if( row->slot > 0 )
    {
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, " | slot %d | %s:%s", row->slot,
            Editor_PaletteKindName(row->kind), row->record);
        if( row->shape >= 0 )
            at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, " | shape %d rot %d", row->shape, row->rotation);
    }
    if( row->note[0] )
        at += snprintf(buffer + at, sizeof(buffer) - (size_t)at, " | note %s", row->note);
    (void)at;
    return snprintf(out, out_capacity, "%s", buffer);
}

int
Editor_PaletteRowValid(const struct Editor_PaletteRow* row)
{
    assert(row);
    if( !Editor_PaletteNameValid(row->palette) || !note_valid(row->note) )
        return 0;
    if( row->slot == 0 )
    {
        if( row->kind != -1 || row->record[0] || row->shape != -1 )
            return 0;
    }
    else
    {
        if( row->slot < 1 || row->slot > EDITOR_PALETTE_SLOTS )
            return 0;
        if( row->kind < EDITOR_PALETTE_LOC || row->kind > EDITOR_PALETTE_OBJ || !Editor_PaletteRecordValid(row->record) )
            return 0;
        if( row->shape != -1 )
        {
            if( row->kind != EDITOR_PALETTE_LOC || row->shape < 0 || row->shape > EDITOR_PALETTE_SHAPE_MAX )
                return 0;
            if( row->rotation < 0 || row->rotation > 3 )
                return 0;
        }
        else if( row->rotation != 0 )
            return 0;
    }
    return Editor_PaletteFormat(row, NULL, 0) <= EDITOR_PALETTES_LINE_MAX;
}

/** A decimal integer at *at (spaces skipped first), at most `max`. */
static int
read_int(
    const char** at,
    const char* end,
    int max,
    int* out)
{
    long value = 0;
    int digits = 0;

    while( *at < end && is_space(**at) )
        (*at)++;
    while( *at < end && **at >= '0' && **at <= '9' )
    {
        value = value * 10 + (**at - '0');
        if( value > max )
            return 0;
        (*at)++;
        digits++;
    }
    *out = (int)value;
    return digits > 0;
}

/** "<word> rest": 1 when [begin, end) starts with `word` then a space or its
 *  end; *rest is past the word, spaces skipped. */
static int
segment_is(
    const char* begin,
    const char* end,
    const char* word,
    const char** rest)
{
    size_t const length = strlen(word);
    if( (size_t)(end - begin) < length || memcmp(begin, word, length) != 0 )
        return 0;
    if( begin + length < end && !is_space(begin[length]) )
        return 0;
    *rest = begin + length;
    while( *rest < end && is_space(**rest) )
        (*rest)++;
    return 1;
}

static int
copy_run(
    char* out,
    size_t out_size,
    const char* begin,
    const char* end)
{
    if( (size_t)(end - begin) >= out_size )
        return 0;
    memcpy(out, begin, (size_t)(end - begin));
    out[end - begin] = '\0';
    return 1;
}

enum
{
    SEEN_SLOT = 1,
    SEEN_RECORD = 2,
    SEEN_SHAPE = 4,
    SEEN_NOTE = 8,
};

static int
parse_segment(
    struct Editor_PaletteRow* row,
    const char* begin,
    const char* end,
    unsigned* seen)
{
    const char* rest;

    if( segment_is(begin, end, "slot", &rest) )
    {
        int slot;
        if( *seen & SEEN_SLOT )
            return 0;
        *seen |= SEEN_SLOT;
        if( !read_int(&rest, end, EDITOR_PALETTE_SLOTS, &slot) || rest != end || slot < 1 )
            return 0;
        row->slot = slot;
        return 1;
    }
    if( segment_is(begin, end, "shape", &rest) )
    {
        int shape;
        int rotation;
        const char* rot;
        if( *seen & SEEN_SHAPE )
            return 0;
        *seen |= SEEN_SHAPE;
        if( !read_int(&rest, end, EDITOR_PALETTE_SHAPE_MAX, &shape) )
            return 0;
        while( rest < end && is_space(*rest) )
            rest++;
        if( !segment_is(rest, end, "rot", &rot) || !read_int(&rot, end, 3, &rotation) || rot != end )
            return 0;
        row->shape = shape;
        row->rotation = rotation;
        return 1;
    }
    if( segment_is(begin, end, "note", &rest) )
    {
        if( *seen & SEEN_NOTE )
            return 0;
        *seen |= SEEN_NOTE;
        return copy_run(row->note, sizeof(row->note), rest, end);
    }
    /* <kind>:<record> */
    if( end - begin > 4 && begin[3] == ':' )
    {
        char kind[4];
        int k;
        memcpy(kind, begin, 3);
        kind[3] = '\0';
        k = Editor_PaletteKindFromName(kind);
        if( k < 0 || (*seen & SEEN_RECORD) )
            return 0;
        *seen |= SEEN_RECORD;
        if( !copy_run(row->record, sizeof(row->record), begin + 4, end) || !Editor_PaletteRecordValid(row->record) )
            return 0;
        row->kind = k;
        return 1;
    }
    return 0;
}

int
Editor_PalettesParseLine(
    const char* text,
    struct Editor_PaletteRow* out_row)
{
    struct Editor_PaletteRow row;
    const char* begin = text;
    const char* end;
    const char* rest;
    const char* bar;
    unsigned seen = 0;

    assert(text);

    end = text + strlen(text);
    trim(&begin, &end);
    if( begin == end || begin[0] == '#' || (end - begin >= 2 && begin[0] == '/' && begin[1] == '/') )
        return EDITOR_PALETTES_LINE_TEXT;
    if( !segment_is(begin, end, "palette", &rest) )
        return -1;

    memset(&row, 0, sizeof(row));
    row.kind = -1;
    row.shape = -1;

    /* The name runs to the first bar. */
    bar = memchr(rest, '|', (size_t)(end - rest));
    {
        const char* name_end = bar ? bar : end;
        const char* name_begin = rest;
        trim(&name_begin, &name_end);
        if( !copy_run(row.palette, sizeof(row.palette), name_begin, name_end) || !Editor_PaletteNameValid(row.palette) )
            return -1;
    }
    while( bar )
    {
        const char* seg = bar + 1;
        const char* seg_end;
        bar = memchr(seg, '|', (size_t)(end - seg));
        seg_end = bar ? bar : end;
        trim(&seg, &seg_end);
        if( !parse_segment(&row, seg, seg_end, &seen) )
            return -1;
    }
    /* A slot and a record come together (an entry) or not at all (a header);
     * a pose belongs to a loc entry. */
    if( ((seen & SEEN_SLOT) != 0) != ((seen & SEEN_RECORD) != 0) )
        return -1;
    if( (seen & SEEN_SHAPE) && row.kind != EDITOR_PALETTE_LOC )
        return -1;
    if( out_row )
        *out_row = row;
    return row.slot > 0 ? EDITOR_PALETTES_LINE_ENTRY : EDITOR_PALETTES_LINE_HEADER;
}

/** Classify `line->text` and attach its row. 0 when it does not parse. */
static int
classify(struct Editor_PalettesLine* line)
{
    struct Editor_PaletteRow row;
    int const kind = Editor_PalettesParseLine(line->text, &row);

    line->kind = EDITOR_PALETTES_LINE_TEXT;
    if( kind == EDITOR_PALETTES_LINE_HEADER || kind == EDITOR_PALETTES_LINE_ENTRY )
    {
        line->kind = (enum Editor_PalettesLineKind)kind;
        line->row = ToriMem_Alloc(ToriMem_Editor(), sizeof(*line->row));
        assert(line->row);
        *line->row = row;
    }
    return kind >= 0;
}

/** The line holding key (name, slot) -- slot 0 the header -- other than
 *  `skip`, or -1. */
static int
find_key(
    const struct Editor_PalettesFile* file,
    const char* name,
    int slot,
    int skip)
{
    for( int i = 0; i < file->line_count; i++ )
    {
        const struct Editor_PaletteRow* row = file->lines[i].row;
        if( i != skip && row && row->slot == slot && strcmp(row->palette, name) == 0 )
            return i;
    }
    return -1;
}

struct Editor_PalettesParseResult
Editor_PalettesParse(
    struct Editor_PalettesFile* file,
    const char* text,
    size_t length)
{
    struct Editor_PalettesParseResult result = { 1, 0 };
    size_t at = 0;
    int line_number = 0;

    assert(file);
    assert(text || length == 0);

    Editor_PalettesFree(file);
    while( at < length )
    {
        const char* begin = text + at;
        const char* newline = memchr(begin, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - begin) : length - at;
        struct Editor_PalettesLine* line = insert_slot(file, file->line_count);
        int ok;

        line_number++;
        line->text = copy_text(begin, line_length);
        ok = classify(line);
        /* Keys are unique: a second header, or a second entry in one slot,
         * would make every by-key command ambiguous. */
        if( ok && line->row && find_key(file, line->row->palette, line->row->slot, file->line_count - 1) >= 0 )
            ok = 0;
        if( !ok && result.ok )
        {
            result.ok = 0;
            result.line = line_number;
        }
        at += line_length;
        if( newline )
        {
            at++;
            file->final_newline = at == length;
        }
        else
            file->final_newline = 0;
    }
    return result;
}

size_t
Editor_PalettesEmit(
    const struct Editor_PalettesFile* file,
    char* out,
    size_t out_capacity)
{
    size_t total = 0;

    assert(file);
    assert(out || out_capacity == 0);

    for( int i = 0; i < file->line_count; i++ )
    {
        const char* text = file->lines[i].text;
        size_t const length = strlen(text);
        int const newline = i + 1 < file->line_count || file->final_newline;

        if( out && total < out_capacity )
        {
            size_t const room = out_capacity - total;
            memcpy(out + total, text, length < room ? length : room);
        }
        total += length;
        if( newline )
        {
            if( out && total < out_capacity )
                out[total] = '\n';
            total++;
        }
    }
    if( out_capacity > 0 )
        out[total < out_capacity ? total : out_capacity - 1] = '\0';
    return total;
}

void
Editor_PalettesCopy(
    struct Editor_PalettesFile* dst,
    const struct Editor_PalettesFile* src)
{
    assert(dst);
    assert(src);
    assert(dst != src);
    Editor_PalettesFree(dst);
    for( int i = 0; i < src->line_count; i++ )
    {
        struct Editor_PalettesLine* line = insert_slot(dst, dst->line_count);
        line->kind = src->lines[i].kind;
        line->text = copy_text(src->lines[i].text, strlen(src->lines[i].text));
        if( src->lines[i].row )
        {
            line->row = ToriMem_Alloc(ToriMem_Editor(), sizeof(*line->row));
            assert(line->row);
            *line->row = *src->lines[i].row;
        }
    }
    dst->final_newline = src->final_newline;
}

int
Editor_PalettesFindHeader(
    const struct Editor_PalettesFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    return find_key(file, name, 0, -1);
}

int
Editor_PalettesFindEntry(
    const struct Editor_PalettesFile* file,
    const char* name,
    int slot)
{
    assert(file);
    assert(name);
    assert(slot >= 1);
    assert(slot <= EDITOR_PALETTE_SLOTS);
    return find_key(file, name, slot, -1);
}

const struct Editor_PaletteRow*
Editor_PalettesEntry(
    const struct Editor_PalettesFile* file,
    const char* name,
    int slot)
{
    int const line = Editor_PalettesFindEntry(file, name, slot);
    return line >= 0 ? file->lines[line].row : NULL;
}

/** 1 when line `index` is the first line naming its palette. */
static int
first_of_palette(
    const struct Editor_PalettesFile* file,
    int index)
{
    const struct Editor_PaletteRow* row = file->lines[index].row;
    if( !row )
        return 0;
    for( int i = 0; i < index; i++ )
        if( file->lines[i].row && strcmp(file->lines[i].row->palette, row->palette) == 0 )
            return 0;
    return 1;
}

int
Editor_PalettesCount(const struct Editor_PalettesFile* file)
{
    int n = 0;
    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        n += first_of_palette(file, i);
    return n;
}

const char*
Editor_PalettesName(
    const struct Editor_PalettesFile* file,
    int nth)
{
    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        if( first_of_palette(file, i) && nth-- == 0 )
            return file->lines[i].row->palette;
    return NULL;
}

int
Editor_PalettesHas(
    const struct Editor_PalettesFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].row && strcmp(file->lines[i].row->palette, name) == 0 )
            return 1;
    return 0;
}

int
Editor_PalettesSlotCount(
    const struct Editor_PalettesFile* file,
    const char* name)
{
    int n = 0;
    assert(file);
    assert(name);
    for( int i = 0; i < file->line_count; i++ )
        n += file->lines[i].row && file->lines[i].row->slot > 0 && strcmp(file->lines[i].row->palette, name) == 0;
    return n;
}

int
Editor_PalettesFirstFreeSlot(
    const struct Editor_PalettesFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    for( int slot = 1; slot <= EDITOR_PALETTE_SLOTS; slot++ )
        if( find_key(file, name, slot, -1) < 0 )
            return slot;
    return 0;
}

/* ---- commands ------------------------------------------------------------------ */

static void
fill_command(
    struct Editor_Cmd* out,
    enum Editor_CmdKind kind,
    const char* before,
    const char* after,
    int line)
{
    size_t const before_len = before ? strlen(before) : 0;
    size_t const after_len = after ? strlen(after) : 0;

    assert(before_len <= EDITOR_PALETTES_LINE_MAX);
    assert(after_len <= EDITOR_PALETTES_LINE_MAX);

    Editor_CmdInit(out, EDITOR_DOC_PALETTES, EDITOR_PALETTES_DOC_KEY, kind);
    out->has_before = before != NULL;
    out->has_after = after != NULL;
    out->u.area.line = line;
    out->u.area.before_len = (uint32_t)before_len;
    out->u.area.after_len = (uint32_t)after_len;
    if( before_len > 0 )
        memcpy(out->u.area.text, before, before_len);
    if( after_len > 0 )
        memcpy(out->u.area.text + before_len, after, after_len);
}

int
Editor_PaletteSetCommand(
    struct Editor_Cmd* out,
    const struct Editor_PalettesFile* file,
    int old_line,
    const struct Editor_PaletteRow* row)
{
    char text[EDITOR_PALETTES_LINE_MAX + 1];

    assert(out);
    assert(file);
    assert(row);
    assert(old_line >= -1);
    assert(old_line < file->line_count);

    if( !Editor_PaletteRowValid(row) )
        return 0;
    if( old_line >= 0 && (!file->lines[old_line].row || strlen(file->lines[old_line].text) > EDITOR_PALETTES_LINE_MAX) )
        return 0;
    Editor_PaletteFormat(row, text, sizeof(text));
    if( old_line >= 0 && strcmp(file->lines[old_line].text, text) == 0 )
        return 0; /* nothing would change */
    if( find_key(file, row->palette, row->slot, old_line) >= 0 )
        return 0;
    fill_command(out, EDITOR_CMD_PALETTE_SET, old_line >= 0 ? file->lines[old_line].text : NULL, text, old_line);
    return 1;
}

int
Editor_PaletteRemoveCommand(
    struct Editor_Cmd* out,
    const struct Editor_PalettesFile* file,
    int line)
{
    assert(out);
    assert(file);
    assert(line >= 0);
    assert(line < file->line_count);

    if( !file->lines[line].row )
        return 0;
    /* A row hand-written past the command's room cannot ride in one; it is
     * removed by editing the file. */
    if( strlen(file->lines[line].text) > EDITOR_PALETTES_LINE_MAX )
        return 0;
    fill_command(out, EDITOR_CMD_PALETTE_REMOVE, file->lines[line].text, NULL, line);
    return 1;
}

/* ---- plans --------------------------------------------------------------------- */

struct Plan
{
    struct Editor_Cmd* out;
    int capacity;
    int count;
    struct Editor_PalettesFile work;
    int failed;
};

static void
plan_begin(
    struct Plan* plan,
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file)
{
    memset(plan, 0, sizeof(*plan));
    plan->out = out;
    plan->capacity = capacity;
    Editor_PalettesInit(&plan->work);
    Editor_PalettesCopy(&plan->work, file);
}

/** Take the command just written at out[count] onto the working copy. */
static void
plan_take(
    struct Plan* plan,
    int built)
{
    int applied;

    if( !built || plan->failed )
    {
        plan->failed = 1;
        return;
    }
    applied = Editor_PalettesApply(&plan->work, &plan->out[plan->count], 0);
    assert(applied);
    (void)applied;
    plan->count++;
}

static int
plan_room(struct Plan* plan)
{
    if( plan->failed || plan->count >= plan->capacity )
    {
        plan->failed = 1;
        return 0;
    }
    return 1;
}

static int
plan_end(struct Plan* plan)
{
    int const count = plan->failed ? 0 : plan->count;
    Editor_PalettesFree(&plan->work);
    return count;
}

int
Editor_PalettePlanAdd(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file,
    const struct Editor_PaletteRow* row)
{
    struct Plan plan;

    assert(out);
    assert(file);
    assert(row);
    assert(capacity >= 0);
    plan_begin(&plan, out, capacity, file);
    if( row->slot < 1 || row->slot > EDITOR_PALETTE_SLOTS )
        plan.failed = 1;
    if( plan_room(&plan) )
        plan_take(&plan,
            Editor_PaletteSetCommand(&out[plan.count], &plan.work, find_key(&plan.work, row->palette, row->slot, -1), row));
    return plan_end(&plan);
}

int
Editor_PalettePlanRename(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file,
    const char* old_name,
    const char* new_name)
{
    struct Plan plan;

    assert(out);
    assert(file);
    assert(old_name);
    assert(new_name);
    plan_begin(&plan, out, capacity, file);
    if( !Editor_PaletteNameValid(new_name) || strcmp(old_name, new_name) == 0 || !Editor_PalettesHas(file, old_name)
        || Editor_PalettesHas(file, new_name) )
        plan.failed = 1;
    for( int i = 0; i < plan.work.line_count && !plan.failed; i++ )
    {
        struct Editor_PaletteRow row;
        if( !plan.work.lines[i].row || strcmp(plan.work.lines[i].row->palette, old_name) != 0 )
            continue;
        row = *plan.work.lines[i].row;
        snprintf(row.palette, sizeof(row.palette), "%s", new_name);
        if( plan_room(&plan) )
            plan_take(&plan, Editor_PaletteSetCommand(&out[plan.count], &plan.work, i, &row));
    }
    return plan_end(&plan);
}

int
Editor_PalettePlanDelete(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file,
    const char* name)
{
    struct Plan plan;

    assert(out);
    assert(file);
    assert(name);
    plan_begin(&plan, out, capacity, file);
    if( !Editor_PalettesHas(file, name) )
        plan.failed = 1;
    /* Header first, so the stroke's first row names the deletion; each
     * remove is taken before the next is found, so every one carries the
     * index it stood at when it went (the undo walks them back in reverse). */
    {
        int const header = find_key(&plan.work, name, 0, -1);
        if( header >= 0 && plan_room(&plan) )
            plan_take(&plan, Editor_PaletteRemoveCommand(&out[plan.count], &plan.work, header));
    }
    for( int i = 0; i < plan.work.line_count && !plan.failed; )
    {
        if( plan.work.lines[i].row && strcmp(plan.work.lines[i].row->palette, name) == 0 )
        {
            if( plan_room(&plan) )
                plan_take(&plan, Editor_PaletteRemoveCommand(&out[plan.count], &plan.work, i));
            /* The emptied file may have lost its header lines too: start
             * over rather than reason about which indices moved. */
            i = 0;
            continue;
        }
        i++;
    }
    return plan_end(&plan);
}

int
Editor_PalettePlanMove(
    struct Editor_Cmd* out,
    int capacity,
    const struct Editor_PalettesFile* file,
    const char* name,
    int from,
    int to)
{
    struct Plan plan;
    struct Editor_PaletteRow moving;
    struct Editor_PaletteRow displaced;
    int from_line;
    int to_line;

    assert(out);
    assert(file);
    assert(name);
    plan_begin(&plan, out, capacity, file);
    if( from < 1 || from > EDITOR_PALETTE_SLOTS || to < 1 || to > EDITOR_PALETTE_SLOTS || from == to )
        return plan_end(&plan);
    from_line = find_key(&plan.work, name, from, -1);
    if( from_line < 0 )
    {
        plan.failed = 1;
        return plan_end(&plan);
    }
    moving = *plan.work.lines[from_line].row;
    moving.slot = to;
    to_line = find_key(&plan.work, name, to, -1);
    if( to_line < 0 )
    {
        if( plan_room(&plan) )
            plan_take(&plan, Editor_PaletteSetCommand(&out[plan.count], &plan.work, from_line, &moving));
        return plan_end(&plan);
    }
    /* A swap: the displaced entry leaves, the moving one takes its slot, the
     * displaced one comes back in the slot the other left. */
    displaced = *plan.work.lines[to_line].row;
    displaced.slot = from;
    if( plan_room(&plan) )
        plan_take(&plan, Editor_PaletteRemoveCommand(&out[plan.count], &plan.work, to_line));
    if( plan_room(&plan) )
        plan_take(&plan, Editor_PaletteSetCommand(&out[plan.count], &plan.work,
                             find_key(&plan.work, name, from, -1), &moving));
    if( plan_room(&plan) )
        plan_take(&plan, Editor_PaletteSetCommand(&out[plan.count], &plan.work, -1, &displaced));
    return plan_end(&plan);
}

void
Editor_PalettesCmdLine(
    const struct Editor_Cmd* command,
    int after,
    char* out,
    size_t out_size)
{
    uint32_t length;
    const char* at;

    assert(command);
    assert(out);
    assert(out_size > 0);

    length = after ? command->u.area.after_len : command->u.area.before_len;
    at = command->u.area.text + (after ? command->u.area.before_len : 0);
    if( command->u.area.before_len > EDITOR_CMD_SPLICE_MAX
        || command->u.area.after_len > EDITOR_CMD_SPLICE_MAX - command->u.area.before_len )
        length = 0;
    if( length >= out_size )
        length = (uint32_t)out_size - 1;
    memcpy(out, at, length);
    out[length] = '\0';
}

/** The file holds nothing but the header lines. */
static int
only_header(const struct Editor_PalettesFile* file)
{
    if( file->line_count != PALETTES_HEADER_LINES )
        return 0;
    for( int i = 0; i < PALETTES_HEADER_LINES; i++ )
        if( strcmp(file->lines[i].text, k_header[i]) != 0 )
            return 0;
    return 1;
}

/** Where a new line of `row`'s palette goes: a header before the palette's
 *  first line, an entry after its last, else the end. */
static int
insert_index(
    const struct Editor_PalettesFile* file,
    const struct Editor_PaletteRow* row)
{
    int first = -1;
    int last = -1;

    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].row && strcmp(file->lines[i].row->palette, row->palette) == 0 )
        {
            if( first < 0 )
                first = i;
            last = i;
        }
    if( first < 0 )
        return file->line_count;
    return row->slot == 0 ? first : last + 1;
}

/** Insert record line `text` at `index` (-1: where insert_index puts it). An
 *  empty file gets the header first. */
static void
insert_record(
    struct Editor_PalettesFile* file,
    int index,
    const char* text)
{
    struct Editor_PalettesLine* line;
    struct Editor_PaletteRow row;
    int ok;

    if( file->line_count == 0 )
    {
        for( int i = 0; i < PALETTES_HEADER_LINES; i++ )
        {
            struct Editor_PalettesLine* header = insert_slot(file, file->line_count);
            header->text = copy_text(k_header[i], strlen(k_header[i]));
            header->kind = EDITOR_PALETTES_LINE_TEXT;
        }
        file->final_newline = 1;
    }
    if( index < 0 || index > file->line_count )
    {
        ok = Editor_PalettesParseLine(text, &row) > 0;
        assert(ok);
        index = insert_index(file, &row);
    }
    line = insert_slot(file, index);
    line->text = copy_text(text, strlen(text));
    ok = classify(line);
    assert(ok);
    (void)ok;
}

static void
remove_record(
    struct Editor_PalettesFile* file,
    int index)
{
    remove_slot(file, index);
    if( only_header(file) )
    {
        while( file->line_count > 0 )
            remove_slot(file, file->line_count - 1);
    }
}

static void
replace_record(
    struct Editor_PalettesFile* file,
    int index,
    const char* text)
{
    struct Editor_PalettesLine* line = &file->lines[index];
    int ok;

    line_free(line);
    line->text = copy_text(text, strlen(text));
    ok = classify(line);
    assert(ok);
    (void)ok;
}

int
Editor_PalettesApply(
    struct Editor_PalettesFile* file,
    const struct Editor_Cmd* command,
    int inverse)
{
    char before[EDITOR_CMD_SPLICE_MAX + 1];
    char after[EDITOR_CMD_SPLICE_MAX + 1];
    struct Editor_PaletteRow expect_row;
    struct Editor_PaletteRow want_row;
    int has_expect;
    int has_want;
    const char* expect;
    const char* want;
    int at = -1;
    int is_set;

    assert(file);
    assert(command);

    if( command->kind != EDITOR_CMD_PALETTE_SET && command->kind != EDITOR_CMD_PALETTE_REMOVE )
        return 0;
    if( command->u.area.before_len > EDITOR_PALETTES_LINE_MAX || command->u.area.after_len > EDITOR_PALETTES_LINE_MAX )
        return 0;
    is_set = command->kind == EDITOR_CMD_PALETTE_SET;
    /* The shape a command may have: SET has an after (and a before when it
     * rewrites a line), REMOVE only a before. */
    if( is_set ? !command->has_after : (!command->has_before || command->has_after) )
        return 0;
    Editor_PalettesCmdLine(command, 0, before, sizeof(before));
    Editor_PalettesCmdLine(command, 1, after, sizeof(after));

    has_expect = inverse ? command->has_after : command->has_before;
    has_want = inverse ? command->has_before : command->has_after;
    expect = inverse ? after : before;
    want = inverse ? before : after;

    /* Both lines must be headers or entries; their keys find the line. */
    if( has_expect )
    {
        if( Editor_PalettesParseLine(expect, &expect_row) <= 0 )
            return 0;
        at = find_key(file, expect_row.palette, expect_row.slot, -1);
        if( at < 0 || strcmp(file->lines[at].text, expect) != 0 )
            return 0;
    }
    if( has_want )
    {
        if( Editor_PalettesParseLine(want, &want_row) <= 0 )
            return 0;
        if( find_key(file, want_row.palette, want_row.slot, at) >= 0 )
            return 0;
    }

    if( has_expect && has_want )
        replace_record(file, at, want);
    else if( has_expect )
        remove_record(file, at);
    else
        /* An undone remove goes back where it stood; an add where its
         * palette's lines are. */
        insert_record(file, is_set ? -1 : command->u.area.line, want);
    return 1;
}

void
Editor_PalettesCmdLabel(
    const struct Editor_Cmd* command,
    char* out,
    size_t out_size)
{
    char before[EDITOR_CMD_SPLICE_MAX + 1];
    char after[EDITOR_CMD_SPLICE_MAX + 1];
    struct Editor_PaletteRow a;
    struct Editor_PaletteRow b;

    assert(command);
    assert(out);
    assert(out_size > 0);

    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    Editor_PalettesCmdLine(command, 0, before, sizeof(before));
    Editor_PalettesCmdLine(command, 1, after, sizeof(after));
    if( command->has_before )
        (void)Editor_PalettesParseLine(before, &a);
    if( command->has_after )
        (void)Editor_PalettesParseLine(after, &b);

    if( command->has_before && !command->has_after )
    {
        if( a.slot == 0 )
            snprintf(out, out_size, "Delete palette %s", a.palette);
        else
            snprintf(out, out_size, "Remove %s from palette %s slot %d", a.record, a.palette, a.slot);
    }
    else if( !command->has_before )
    {
        if( b.slot == 0 )
            snprintf(out, out_size, "New palette %s", b.palette);
        else
            snprintf(out, out_size, "Add %s to palette %s slot %d", b.record, b.palette, b.slot);
    }
    else if( strcmp(a.palette, b.palette) != 0 )
        snprintf(out, out_size, "Rename palette %s to %s", a.palette, b.palette);
    else if( a.slot == 0 || b.slot == 0 )
        snprintf(out, out_size, "Edit palette %s", b.palette);
    else if( a.slot != b.slot )
        snprintf(out, out_size, "Move %s to slot %d of palette %s", b.record, b.slot, b.palette);
    else if( a.kind != b.kind || strcmp(a.record, b.record) != 0 )
        snprintf(out, out_size, "Replace %s with %s in palette %s slot %d", a.record, b.record, b.palette, b.slot);
    else if( a.shape != b.shape || a.rotation != b.rotation )
    {
        if( b.shape < 0 )
            snprintf(out, out_size, "Clear pose of %s in palette %s slot %d", b.record, b.palette, b.slot);
        else
            snprintf(out, out_size, "Set pose of %s in palette %s slot %d to shape %d rot %d", b.record, b.palette,
                b.slot, b.shape, b.rotation);
    }
    else
        snprintf(out, out_size, "Edit palette %s slot %d", b.palette, b.slot);
}
