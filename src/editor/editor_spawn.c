/**
 * The `.spawn` codec. See editor_spawn.h for the grammar and why it keeps
 * lines rather than records.
 */

#include "editor_spawn.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
Editor_SpawnInit(struct Editor_SpawnFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

void
Editor_SpawnFree(struct Editor_SpawnFile* file)
{
    if( !file )
        return;

    for( int i = 0; i < file->line_count; i++ )
        ToriMem_Free(ToriMem_Editor(), file->lines[i].text);
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

/** Open a slot at `index`, shifting the tail down. Returns the slot. */
static struct Editor_SpawnLine*
insert_slot(
    struct Editor_SpawnFile* file,
    int index)
{
    assert(file);
    assert(index >= 0);
    assert(index <= file->line_count);

    if( file->line_count == file->line_capacity )
    {
        int next = file->line_capacity ? file->line_capacity * 2 : 64;
        file->lines = ToriMem_Realloc(
            ToriMem_Editor(), file->lines, (size_t)next * sizeof(*file->lines));
        assert(file->lines);
        file->line_capacity = next;
    }
    memmove(
        &file->lines[index + 1],
        &file->lines[index],
        (size_t)(file->line_count - index) * sizeof(*file->lines));
    file->line_count++;
    memset(&file->lines[index], 0, sizeof(file->lines[index]));
    return &file->lines[index];
}

/**
 * Classify one line and, for a row, read its fields. `text` is the line
 * without its newline. Returns 0 when the line is a row that does not parse.
 */
static int
classify_line(
    struct Editor_SpawnLine* line,
    const char* text,
    enum Editor_SpawnSection* section)
{
    char clean[512];
    char* start;
    char* comment;
    size_t length;
    char name[EDITOR_CMD_NAME_MAX + 2];
    int x;
    int z;
    int level;
    int count = 1;
    int fields;
    char extra;

    snprintf(clean, sizeof(clean), "%s", text);
    comment = strstr(clean, "//");
    if( comment )
        *comment = '\0';
    start = clean;
    while( *start == ' ' || *start == '\t' )
        start++;
    length = strlen(start);
    while( length > 0 && (unsigned char)start[length - 1] <= ' ' )
        start[--length] = '\0';

    line->section = *section;
    if( length == 0 )
    {
        line->kind = EDITOR_SPAWN_LINE_TEXT;
        return 1;
    }
    if( strncmp(start, "====", 4) == 0 )
    {
        line->kind = EDITOR_SPAWN_LINE_HEADER;
        if( strstr(start, "NPC") )
            *section = EDITOR_SPAWN_SECTION_NPC;
        else if( strstr(start, "OBJ") )
            *section = EDITOR_SPAWN_SECTION_OBJ;
        else
            return 0;
        line->section = *section;
        return 1;
    }
    if( *section == EDITOR_SPAWN_SECTION_NONE )
        return 0;

    /* %65s: one past the name bound, so an over-long name is caught by the
     * length check below rather than silently truncated. */
    fields = sscanf(start, "%65s %d %d %d %d %c", name, &x, &z, &level, &count, &extra);
    if( fields < 4 || fields > 5 )
        return 0;
    if( *section == EDITOR_SPAWN_SECTION_NPC && fields != 4 )
        return 0;
    if( level < 0 || level > 3 || count < 1 )
        return 0;

    line->kind = EDITOR_SPAWN_LINE_ROW;
    line->row.is_obj = *section == EDITOR_SPAWN_SECTION_OBJ;
    line->row.suppress = name[0] == '-';
    {
        const char* bare = line->row.suppress ? name + 1 : name;
        if( bare[0] == '\0' || strlen(bare) >= sizeof(line->row.name) )
            return 0;
        snprintf(line->row.name, sizeof(line->row.name), "%s", bare);
    }
    line->row.abs_x = x;
    line->row.abs_z = z;
    line->row.level = level;
    line->row.count = count;
    return 1;
}

struct Editor_SpawnParseResult
Editor_SpawnParse(
    struct Editor_SpawnFile* file,
    const char* text,
    size_t length)
{
    struct Editor_SpawnParseResult result = { 1, 0 };
    enum Editor_SpawnSection section = EDITOR_SPAWN_SECTION_NONE;
    size_t at = 0;
    int line_number = 0;

    assert(file);
    assert(text || length == 0);

    Editor_SpawnFree(file);

    while( at < length )
    {
        const char* begin = text + at;
        const char* newline = memchr(begin, '\n', length - at);
        size_t line_length = newline ? (size_t)(newline - begin) : length - at;
        struct Editor_SpawnLine* line = insert_slot(file, file->line_count);

        line_number++;
        line->text = copy_text(begin, line_length);
        if( !classify_line(line, line->text, &section) && result.ok )
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
Editor_SpawnEmit(
    const struct Editor_SpawnFile* file,
    char* out,
    size_t out_capacity)
{
    size_t total = 0;

    assert(file);
    assert(out || out_capacity == 0);

    for( int i = 0; i < file->line_count; i++ )
    {
        const char* text = file->lines[i].text;
        size_t length = strlen(text);
        int newline = i + 1 < file->line_count || file->final_newline;

        if( out && total < out_capacity )
        {
            size_t room = out_capacity - total;
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

static int
rows_equal(
    const struct Editor_SpawnRow* a,
    const struct Editor_SpawnRow* b)
{
    return a->is_obj == b->is_obj && a->suppress == b->suppress
           && strcmp(a->name, b->name) == 0 && a->abs_x == b->abs_x && a->abs_z == b->abs_z
           && a->level == b->level && (!a->is_obj || a->count == b->count);
}

int
Editor_SpawnFind(
    const struct Editor_SpawnFile* file,
    const struct Editor_SpawnRow* row)
{
    assert(file);
    assert(row);

    for( int i = 0; i < file->line_count; i++ )
    {
        if( file->lines[i].kind == EDITOR_SPAWN_LINE_ROW && rows_equal(&file->lines[i].row, row) )
            return i;
    }
    return -1;
}

int
Editor_SpawnFormatRow(
    const struct Editor_SpawnRow* row,
    char* out,
    size_t out_capacity)
{
    char name[EDITOR_CMD_NAME_MAX + 2];

    assert(row);
    assert(out);

    snprintf(name, sizeof(name), "%s%s", row->suppress ? "-" : "", row->name);
    if( row->is_obj )
        return snprintf(
            out,
            out_capacity,
            "%-44s %5d %5d %d %d",
            name,
            row->abs_x,
            row->abs_z,
            row->level,
            row->count);
    return snprintf(
        out, out_capacity, "%-44s %5d %5d %d", name, row->abs_x, row->abs_z, row->level);
}

static void
insert_text_line(
    struct Editor_SpawnFile* file,
    int index,
    enum Editor_SpawnLineKind kind,
    enum Editor_SpawnSection section,
    const char* text)
{
    struct Editor_SpawnLine* line = insert_slot(file, index);
    line->kind = kind;
    line->section = section;
    line->text = copy_text(text, strlen(text));
}

int
Editor_SpawnInsert(
    struct Editor_SpawnFile* file,
    const struct Editor_SpawnRow* row)
{
    enum Editor_SpawnSection want;
    int header = -1;
    int index;
    char text[160];
    struct Editor_SpawnLine* line;

    assert(file);
    assert(row);

    want = row->is_obj ? EDITOR_SPAWN_SECTION_OBJ : EDITOR_SPAWN_SECTION_NPC;
    for( int i = 0; i < file->line_count; i++ )
    {
        if( file->lines[i].kind == EDITOR_SPAWN_LINE_HEADER && file->lines[i].section == want )
        {
            header = i;
            break;
        }
    }

    if( header < 0 )
    {
        /* A new section goes at the end, a blank line after whatever came
         * before it — the generator's own layout. */
        if( file->line_count > 0 && file->lines[file->line_count - 1].text[0] != '\0' )
            insert_text_line(
                file,
                file->line_count,
                EDITOR_SPAWN_LINE_TEXT,
                file->lines[file->line_count - 1].section,
                "");
        insert_text_line(
            file,
            file->line_count,
            EDITOR_SPAWN_LINE_HEADER,
            want,
            want == EDITOR_SPAWN_SECTION_OBJ ? "==== OBJ ====" : "==== NPC ====");
        index = file->line_count;
    }
    else
    {
        /* After the section's last non-blank line: past its rows and
         * comments, before the blank separator and the next banner. */
        index = header + 1;
        for( int i = header + 1; i < file->line_count; i++ )
        {
            if( file->lines[i].kind == EDITOR_SPAWN_LINE_HEADER )
                break;
            if( file->lines[i].kind != EDITOR_SPAWN_LINE_TEXT || file->lines[i].text[0] != '\0' )
                index = i + 1;
        }
    }

    Editor_SpawnFormatRow(row, text, sizeof(text));
    line = insert_slot(file, index);
    line->kind = EDITOR_SPAWN_LINE_ROW;
    line->section = want;
    line->row = *row;
    line->text = copy_text(text, strlen(text));
    file->final_newline = 1;
    return index;
}

void
Editor_SpawnRemoveAt(
    struct Editor_SpawnFile* file,
    int index)
{
    assert(file);
    assert(index >= 0);
    assert(index < file->line_count);

    ToriMem_Free(ToriMem_Editor(), file->lines[index].text);
    memmove(
        &file->lines[index],
        &file->lines[index + 1],
        (size_t)(file->line_count - index - 1) * sizeof(*file->lines));
    file->line_count--;
}

int
Editor_SpawnRowCount(const struct Editor_SpawnFile* file)
{
    int count = 0;

    assert(file);

    for( int i = 0; i < file->line_count; i++ )
    {
        if( file->lines[i].kind == EDITOR_SPAWN_LINE_ROW )
            count++;
    }
    return count;
}
