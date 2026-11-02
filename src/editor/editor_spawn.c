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

/* ---- editing a row in place -------------------------------------------------- */

int
Editor_SpawnRowsEqual(
    const struct Editor_SpawnRow* a,
    const struct Editor_SpawnRow* b)
{
    assert(a);
    assert(b);
    return rows_equal(a, b);
}

void
Editor_SpawnRowToCmd(
    const struct Editor_SpawnRow* row,
    struct Editor_CmdSpawnRow* out)
{
    assert(row);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->is_obj = row->is_obj ? 1 : 0;
    out->suppress = row->suppress ? 1 : 0;
    snprintf(out->name, sizeof(out->name), "%s", row->name);
    out->abs_x = row->abs_x;
    out->abs_z = row->abs_z;
    out->level = row->level;
    out->count = row->is_obj ? (row->count < 1 ? 1 : row->count) : 1;
}

void
Editor_SpawnRowFromCmd(
    const struct Editor_CmdSpawnRow* row,
    struct Editor_SpawnRow* out)
{
    assert(row);
    assert(out);
    memset(out, 0, sizeof(*out));
    out->is_obj = row->is_obj ? 1 : 0;
    out->suppress = row->suppress ? 1 : 0;
    snprintf(out->name, sizeof(out->name), "%s", row->name);
    out->abs_x = row->abs_x;
    out->abs_z = row->abs_z;
    out->level = row->level;
    out->count = row->count < 1 ? 1 : row->count;
}

void
Editor_SpawnRowDocKey(
    const struct Editor_SpawnRow* row,
    char out[EDITOR_DOC_KEY_MAX])
{
    assert(row);
    assert(out);
    assert(row->abs_x >= 0);
    assert(row->abs_z >= 0);
    Editor_SquareKey(out, row->abs_x / EDITOR_SQUARE_X, row->abs_z / EDITOR_SQUARE_Z);
}

int
Editor_SpawnRowValid(const struct Editor_SpawnRow* row)
{
    assert(row);

    if( row->name[0] == '\0' || strlen(row->name) >= sizeof(row->name) )
        return 0;
    for( const char* c = row->name; *c; c++ )
        if( (unsigned char)*c <= ' ' || *c == '/' )
            return 0;
    if( row->name[0] == '-' )
        return 0;
    if( row->name[0] == '#' )
    {
        if( row->name[1] == '\0' )
            return 0;
        for( const char* c = row->name + 1; *c; c++ )
            if( *c < '0' || *c > '9' )
                return 0;
    }
    if( row->abs_x < 0 || row->abs_z < 0 || row->level < 0 || row->level > 3 )
        return 0;
    if( row->is_obj && row->count < 1 )
        return 0;
    return 1;
}

/** The trailing `//` comment of a line, with the whitespace before it, or "". */
static const char*
line_comment(const char* text)
{
    const char* comment = strstr(text, "//");
    if( !comment )
        return "";
    while( comment > text && (comment[-1] == ' ' || comment[-1] == '\t') )
        comment--;
    return comment;
}

int
Editor_SpawnReplaceAt(
    struct Editor_SpawnFile* file,
    int index,
    const struct Editor_SpawnRow* row,
    const char* text)
{
    struct Editor_SpawnLine* line;
    struct Editor_SpawnLine probe;
    enum Editor_SpawnSection section;
    char formatted[EDITOR_CMD_VALUE_MAX + 160];

    assert(file);
    assert(row);
    assert(index >= 0);
    assert(index < file->line_count);

    line = &file->lines[index];
    if( line->kind != EDITOR_SPAWN_LINE_ROW )
        return 0;
    if( (row->is_obj ? EDITOR_SPAWN_SECTION_OBJ : EDITOR_SPAWN_SECTION_NPC) != line->section )
        return 0;

    if( text )
        snprintf(formatted, sizeof(formatted), "%s", text);
    else
    {
        int length = Editor_SpawnFormatRow(row, formatted, sizeof(formatted));
        if( length < 0 || (size_t)length >= sizeof(formatted) )
            return 0;
        snprintf(formatted + length, sizeof(formatted) - (size_t)length, "%s", line_comment(line->text));
    }

    /* What gets written must read back as the row the command names. */
    memset(&probe, 0, sizeof(probe));
    section = line->section;
    if( !classify_line(&probe, formatted, &section) || probe.kind != EDITOR_SPAWN_LINE_ROW
        || !rows_equal(&probe.row, row) )
        return 0;

    ToriMem_Free(ToriMem_Editor(), line->text);
    line->text = copy_text(formatted, strlen(formatted));
    line->row = probe.row;
    return 1;
}

void
Editor_SpawnEditCommand(
    struct Editor_Cmd* out,
    const char* doc_key,
    const struct Editor_SpawnRow* before,
    const struct Editor_SpawnRow* after,
    const struct Editor_SpawnFile* file)
{
    assert(out);
    assert(doc_key);
    assert(before);
    assert(after);

    Editor_CmdInit(out, EDITOR_DOC_SPAWN, doc_key, EDITOR_CMD_SPAWN_EDIT);
    out->has_before = 1;
    out->has_after = 1;
    Editor_SpawnRowToCmd(before, &out->u.spawn_edit.before);
    Editor_SpawnRowToCmd(after, &out->u.spawn_edit.after);
    if( file )
    {
        int const index = Editor_SpawnFind(file, before);
        if( index >= 0 && strlen(file->lines[index].text) < sizeof(out->u.spawn_edit.before_text) )
            snprintf(out->u.spawn_edit.before_text, sizeof(out->u.spawn_edit.before_text), "%s",
                file->lines[index].text);
    }
}

int
Editor_SpawnApplyEdit(
    struct Editor_SpawnFile* file,
    const struct Editor_Cmd* command,
    int inverse)
{
    struct Editor_SpawnRow before;
    struct Editor_SpawnRow after;
    const struct Editor_SpawnRow* from;
    const struct Editor_SpawnRow* to;
    const char* text = NULL;
    int index;

    assert(file);
    assert(command);
    assert(command->kind == EDITOR_CMD_SPAWN_EDIT);

    Editor_SpawnRowFromCmd(&command->u.spawn_edit.before, &before);
    Editor_SpawnRowFromCmd(&command->u.spawn_edit.after, &after);
    if( before.is_obj != after.is_obj || before.name[0] == '\0' || after.name[0] == '\0'
        || before.level < 0 || before.level > 3 || after.level < 0 || after.level > 3 )
        return 0;
    from = inverse ? &after : &before;
    to = inverse ? &before : &after;
    index = Editor_SpawnFind(file, from);
    if( index < 0 )
        return 0;
    if( inverse && command->u.spawn_edit.before_text[0] )
    {
        /* The old line's own bytes -- when they still read as the row. */
        text = command->u.spawn_edit.before_text;
        if( Editor_SpawnReplaceAt(file, index, to, text) )
            return 1;
    }
    return Editor_SpawnReplaceAt(file, index, to, NULL);
}

static void
plan_row_command(
    struct Editor_Cmd* out,
    const struct Editor_SpawnRow* row,
    int adding)
{
    char key[EDITOR_DOC_KEY_MAX];

    Editor_SpawnRowDocKey(row, key);
    Editor_CmdInit(out, EDITOR_DOC_SPAWN, key, row->suppress ? EDITOR_CMD_SPAWN_SUPPRESS : EDITOR_CMD_SPAWN);
    out->has_before = adding ? 0 : 1;
    out->has_after = adding ? 1 : 0;
    out->u.spawn.is_obj = row->is_obj ? 1 : 0;
    snprintf(out->u.spawn.name, sizeof(out->u.spawn.name), "%s", row->name);
    out->u.spawn.abs_x = row->abs_x;
    out->u.spawn.abs_z = row->abs_z;
    out->u.spawn.level = row->level;
    out->u.spawn.count = row->count < 1 ? 1 : row->count;
}

int
Editor_SpawnEditPlan(
    const struct Editor_SpawnFile* edited,
    const struct Editor_SpawnEditSource* source,
    const struct Editor_SpawnRow* want,
    struct Editor_Cmd* out,
    int max)
{
    struct Editor_SpawnRow target;
    struct Editor_SpawnRow old;
    char old_key[EDITOR_DOC_KEY_MAX];
    char new_key[EDITOR_DOC_KEY_MAX];
    int count = 0;

    assert(source);
    assert(want);
    assert(out || max == 0);

    target = *want;
    if( !target.is_obj )
        target.count = 1;
    if( !Editor_SpawnRowValid(&target) )
        return -1;
    old = source->row;
    Editor_SpawnRowDocKey(&old, old_key);
    Editor_SpawnRowDocKey(&target, new_key);

    if( source->edited )
    {
        if( rows_equal(&old, &target) )
            return 0;
        if( old.is_obj == target.is_obj && strcmp(old_key, new_key) == 0 )
        {
            if( max < 1 )
                return -1;
            Editor_SpawnEditCommand(&out[0], old_key, &old, &target, edited);
            return 1;
        }
        if( max < 2 )
            return -1;
        plan_row_command(&out[count++], &old, 0);
        plan_row_command(&out[count++], &target, 1);
        return count;
    }

    /* A generated row: its file is never written. */
    old.suppress = 0;
    {
        struct Editor_SpawnRow fields = target;
        fields.suppress = 0;
        if( rows_equal(&old, &fields) )
        {
            struct Editor_SpawnRow mark = old;
            if( target.suppress == source->suppressed )
                return 0;
            if( max < 1 )
                return -1;
            mark.suppress = 1;
            plan_row_command(&out[0], &mark, target.suppress);
            return 1;
        }
        if( max < (source->suppressed ? 1 : 2) )
            return -1;
        if( !source->suppressed )
        {
            struct Editor_SpawnRow mark = old;
            mark.suppress = 1;
            plan_row_command(&out[count++], &mark, 1);
        }
        plan_row_command(&out[count++], &fields, 1);
        return count;
    }
}

void
Editor_SpawnRemoveRow(
    struct Editor_SpawnFile* file,
    int index)
{
    assert(file);
    assert(index >= 0);
    assert(index < file->line_count);
    assert(file->lines[index].kind == EDITOR_SPAWN_LINE_ROW);

    Editor_SpawnRemoveAt(file, index);
    /* The trailing section Insert opened: "", banner, row -- now "", banner. */
    if( index == file->line_count && index >= 2 && file->lines[index - 1].kind == EDITOR_SPAWN_LINE_HEADER
        && file->lines[index - 2].kind == EDITOR_SPAWN_LINE_TEXT && file->lines[index - 2].text[0] == '\0' )
    {
        Editor_SpawnRemoveAt(file, index - 1);
        Editor_SpawnRemoveAt(file, index - 2);
    }
}
