/*
 * The quest board's reader (editor_quest.h): the parts of one quest, read
 * from the files that already hold them. Nothing here writes.
 */
#include "editor_quest.h"

#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- words ------------------------------------------------------------------- */

const char*
Editor_QuestStateWord(enum Editor_QuestState state)
{
    switch( state )
    {
    case EDITOR_QUEST_OK:
        return "ok";
    case EDITOR_QUEST_MISSING:
        return "missing";
    case EDITOR_QUEST_UNKNOWN:
        return "not indexed";
    case EDITOR_QUEST_NO_TEST_ROW:
        return "no test row";
    case EDITOR_QUEST_NOT_RUN:
        return "not run";
    case EDITOR_QUEST_NOT_GATED:
        return "not gated";
    case EDITOR_QUEST_STALE:
        return "stale";
    case EDITOR_QUEST_GREEN:
        return "green";
    case EDITOR_QUEST_RED:
        return "red";
    case EDITOR_QUEST_BLOCKED:
        return "blocked";
    }
    assert(0 && "Editor_QuestStateWord: not a state");
    return "?";
}

int
Editor_QuestIdValid(const char* id)
{
    size_t length;

    assert(id);
    length = strlen(id);
    if( length == 0 || length >= EDITOR_QUEST_ID_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = id[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

/* ---- files ------------------------------------------------------------------- */

static char*
quest_read(
    const char* path,
    size_t* out_length)
{
    FILE* f;
    long size;
    char* text;

    *out_length = 0;
    f = fopen(path, "rb");
    if( !f )
        return NULL;
    if( fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return NULL;
    }
    text = ToriMem_Alloc(ToriMem_Editor(), (size_t)size + 1);
    assert(text);
    if( fread(text, 1, (size_t)size, f) != (size_t)size )
        size = 0;
    text[size] = '\0';
    fclose(f);
    *out_length = (size_t)size;
    return text;
}

static int64_t
quest_mtime(const char* path)
{
    struct stat info;
    if( stat(path, &info) != 0 )
        return 0;
    return (int64_t)info.st_mtime;
}

static int
quest_exists(const char* path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void
quest_fmt(
    char* out,
    size_t size,
    const char* format,
    ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(out, size, format, args);
    va_end(args);
}

/** The next line of `text` from *at, without its newline (and '\r'). */
static int
quest_line(
    const char* text,
    size_t length,
    size_t* at,
    const char** out_start,
    size_t* out_length)
{
    size_t start = *at;
    size_t stop = start;

    if( start >= length )
        return 0;
    while( stop < length && text[stop] != '\n' )
        stop++;
    *at = stop < length ? stop + 1 : stop;
    if( stop > start && text[stop - 1] == '\r' )
        stop--;
    *out_start = text + start;
    *out_length = stop - start;
    return 1;
}

/* ---- the TSV tables ------------------------------------------------------------- */

/** In TSV `text`, the row whose column `key_column` is `key`: the values of
 *  `want` (column names) into `out` (`out_size` each). 1 when found. */
static int
quest_tsv_row(
    const char* text,
    size_t length,
    const char* key_column,
    const char* key,
    const char* const* want,
    int want_count,
    char (*out)[EDITOR_QUEST_NAME_MAX])
{
    enum
    {
        TSV_COLUMNS = 32
    };
    char names[TSV_COLUMNS][64];
    int name_count = 0;
    int key_index = -1;
    int want_index[8];
    size_t at = 0;
    const char* line;
    size_t line_length;

    assert(want_count <= 8);
    if( !quest_line(text, length, &at, &line, &line_length) )
        return 0;
    for( size_t start = 0; start <= line_length && name_count < TSV_COLUMNS; )
    {
        size_t stop = start;
        while( stop < line_length && line[stop] != '\t' )
            stop++;
        quest_fmt(names[name_count], sizeof(names[0]), "%.*s", (int)(stop - start), line + start);
        name_count++;
        start = stop + 1;
    }
    for( int i = 0; i < want_count; i++ )
        want_index[i] = -1;
    for( int c = 0; c < name_count; c++ )
    {
        if( strcmp(names[c], key_column) == 0 )
            key_index = c;
        for( int i = 0; i < want_count; i++ )
        {
            if( strcmp(names[c], want[i]) == 0 )
                want_index[i] = c;
        }
    }
    if( key_index < 0 )
        return 0;
    while( quest_line(text, length, &at, &line, &line_length) )
    {
        const char* cells[TSV_COLUMNS];
        size_t cell_lengths[TSV_COLUMNS];
        int cell_count = 0;

        for( size_t start = 0; start <= line_length && cell_count < TSV_COLUMNS; )
        {
            size_t stop = start;
            while( stop < line_length && line[stop] != '\t' )
                stop++;
            cells[cell_count] = line + start;
            cell_lengths[cell_count] = stop - start;
            cell_count++;
            start = stop + 1;
        }
        if( key_index >= cell_count || cell_lengths[key_index] != strlen(key)
            || memcmp(cells[key_index], key, cell_lengths[key_index]) != 0 )
            continue;
        for( int i = 0; i < want_count; i++ )
        {
            out[i][0] = '\0';
            if( want_index[i] >= 0 && want_index[i] < cell_count )
                quest_fmt(out[i], EDITOR_QUEST_NAME_MAX, "%.*s", (int)cell_lengths[want_index[i]],
                    cells[want_index[i]]);
        }
        return 1;
    }
    return 0;
}

/* ---- the scripts ------------------------------------------------------------------ */

struct quest_file
{
    /** Relative to the content root. */
    char path[EDITOR_QUEST_PATH_MAX];
    char* text;
    size_t length;
};

struct quest_block
{
    char type[48];
    char subject[EDITOR_QUEST_NAME_MAX];
    int file;
    int line;
    size_t body;
    size_t body_end;
};

struct quest_scan
{
    struct quest_file files[EDITOR_QUEST_FILES_MAX];
    int file_count;
    struct quest_block* blocks;
    int block_count;
    int block_capacity;
};

static int
quest_ident(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static void
quest_scan_add_file(
    struct quest_scan* scan,
    const char* content_root,
    const char* relative)
{
    char path[EDITOR_QUEST_PATH_MAX * 2];
    struct quest_file* file;

    for( int i = 0; i < scan->file_count; i++ )
    {
        if( strcmp(scan->files[i].path, relative) == 0 )
            return;
    }
    if( scan->file_count >= EDITOR_QUEST_FILES_MAX )
        return;
    file = &scan->files[scan->file_count];
    quest_fmt(path, sizeof(path), "%s/%s", content_root, relative);
    file->text = quest_read(path, &file->length);
    if( !file->text )
        return;
    quest_fmt(file->path, sizeof(file->path), "%s", relative);
    scan->file_count++;
}

/** Every `[type,subject]` header of every file: a block runs to the next. */
static void
quest_scan_blocks(struct quest_scan* scan)
{
    for( int f = 0; f < scan->file_count; f++ )
    {
        const struct quest_file* file = &scan->files[f];
        size_t at = 0;
        const char* line;
        size_t length;
        int number = 0;
        struct quest_block* open = NULL;

        while( quest_line(file->text, file->length, &at, &line, &length) )
        {
            const char* close;
            const char* comma;
            number++;
            if( length < 3 || line[0] != '[' )
                continue;
            close = memchr(line, ']', length);
            comma = memchr(line, ',', length);
            if( !close || !comma || comma > close )
                continue;
            if( open )
                open->body_end = (size_t)(line - file->text);
            if( scan->block_count == scan->block_capacity )
            {
                scan->block_capacity = scan->block_capacity ? scan->block_capacity * 2 : 64;
                scan->blocks = ToriMem_Realloc(
                    ToriMem_Editor(), scan->blocks, sizeof(scan->blocks[0]) * (size_t)scan->block_capacity);
                assert(scan->blocks);
            }
            open = &scan->blocks[scan->block_count++];
            memset(open, 0, sizeof(*open));
            quest_fmt(open->type, sizeof(open->type), "%.*s", (int)(comma - line - 1), line + 1);
            quest_fmt(open->subject, sizeof(open->subject), "%.*s", (int)(close - comma - 1), comma + 1);
            open->file = f;
            open->line = number;
            open->body = at;
            open->body_end = file->length;
        }
    }
}

static void
quest_scan_free(struct quest_scan* scan)
{
    for( int i = 0; i < scan->file_count; i++ )
        ToriMem_Free(ToriMem_Editor(), scan->files[i].text);
    ToriMem_Free(ToriMem_Editor(), scan->blocks);
    memset(scan, 0, sizeof(*scan));
}

/** `%<var>` at `p` (either spelling: "varp29_cookquest" or "cookquest"),
 *  returning the text after it, or NULL. */
static const char*
quest_var_at(
    const char* p,
    const char* end,
    const char* var,
    const char* short_var)
{
    const char* names[2] = { var, short_var };

    if( p >= end || *p != '%' )
        return NULL;
    for( int n = 0; n < 2; n++ )
    {
        size_t const length = strlen(names[n]);
        if( !length || (size_t)(end - p - 1) < length || memcmp(p + 1, names[n], length) != 0 )
            continue;
        if( p + 1 + length < end && quest_ident(p[1 + length]) )
            continue;
        return p + 1 + length;
    }
    return NULL;
}

/** `^<constant>` anywhere in [p, end) as a whole word. */
static int
quest_has_constant(
    const char* p,
    const char* end,
    const char* constant)
{
    size_t const length = strlen(constant);
    for( ; p < end; p++ )
    {
        if( *p != '^' || (size_t)(end - p - 1) < length || memcmp(p + 1, constant, length) != 0 )
            continue;
        if( p + 1 + length < end && quest_ident(p[1 + length]) )
            continue;
        return 1;
    }
    return 0;
}

/** The var named anywhere in [p, end). */
static int
quest_has_var(
    const char* p,
    const char* end,
    const char* var,
    const char* short_var)
{
    for( ; p < end; p++ )
    {
        if( quest_var_at(p, end, var, short_var) )
            return 1;
    }
    return 0;
}

enum quest_use
{
    QUEST_USE_SETS = 1,
    QUEST_USE_TESTS = 2,
};

/** How block `b` uses `^constant` with the var: SETS (a statement
 *  `%var = ^c;`), TESTS (any other line naming both, or a `case ^c` under a
 *  `switch_int (%var)`), or 0; the first such line in *out_line. */
static int
quest_block_use(
    const struct quest_scan* scan,
    const struct quest_block* b,
    const char* var,
    const char* short_var,
    const char* constant,
    int* out_line)
{
    const struct quest_file* file = &scan->files[b->file];
    size_t at = b->body;
    const char* line;
    size_t length;
    int number = b->line;
    int use = 0;
    int in_switch = 0;

    while( at < b->body_end && quest_line(file->text, b->body_end, &at, &line, &length) )
    {
        const char* p = line;
        const char* end = line + length;
        const char* after;
        number++;
        while( p < end && (*p == ' ' || *p == '\t') )
            p++;
        if( end - p >= 2 && p[0] == '/' && p[1] == '/' )
            continue;
        if( strncmp(p, "switch_int", 10) == 0 && quest_has_var(p, end, var, short_var) )
            in_switch = 1;
        if( !quest_has_constant(p, end, constant) )
            continue;
        after = quest_var_at(p, end, var, short_var);
        if( after )
        {
            while( after < end && *after == ' ' )
                after++;
            if( after < end && *after == '=' && end > p && end[-1] == ';' )
            {
                if( !use && out_line )
                    *out_line = number;
                use |= QUEST_USE_SETS;
                continue;
            }
        }
        if( quest_has_var(p, end, var, short_var) || (in_switch && strncmp(p, "case", 4) == 0) )
        {
            if( !use && out_line )
                *out_line = number;
            use |= QUEST_USE_TESTS;
        }
    }
    return use;
}

/** The first block of the scan that names `@<subject>` (a label) or
 *  `~<subject>` (a proc) in its body, -1 for none. */
static int
quest_caller(
    const struct quest_scan* scan,
    const struct quest_block* callee,
    const unsigned char* visited)
{
    char sigil;
    size_t const length = strlen(callee->subject);

    if( strcmp(callee->type, "label") == 0 )
        sigil = '@';
    else if( strcmp(callee->type, "proc") == 0 )
        sigil = '~';
    else
        return -1;
    for( int i = 0; i < scan->block_count; i++ )
    {
        const struct quest_block* b = &scan->blocks[i];
        const char* text = scan->files[b->file].text;
        if( i < EDITOR_QUEST_TRIGGERS_MAX && visited[i] )
            continue;
        for( size_t p = b->body; p + 1 + length <= b->body_end; p++ )
        {
            if( text[p] != sigil || memcmp(text + p + 1, callee->subject, length) != 0 )
                continue;
            if( p + 1 + length < b->body_end && quest_ident(text[p + 1 + length]) )
                continue;
            return i;
        }
    }
    return -1;
}

static int
quest_is_op(const char* type)
{
    return (type[0] == 'o' || type[0] == 'a') && type[1] == 'p';
}

/** The dialogue calls in block `b` and in the labels it jumps to (one level). */
static int
quest_dialogue_calls(
    const struct quest_scan* scan,
    int b)
{
    static const char* const k_calls[] = { "~chatnpc", "~chatplayer", "~p_choice", "~mesbox", "~objbox",
        "~doubleobjbox" };
    unsigned char seen[EDITOR_QUEST_TRIGGERS_MAX] = { 0 };
    int queue[16];
    int head = 0;
    int tail = 0;
    int calls = 0;

    queue[tail++] = b;
    if( b < EDITOR_QUEST_TRIGGERS_MAX )
        seen[b] = 1;
    while( head < tail )
    {
        const struct quest_block* block = &scan->blocks[queue[head++]];
        const char* text = scan->files[block->file].text;
        for( size_t p = block->body; p < block->body_end; p++ )
        {
            if( text[p] == '~' )
            {
                for( size_t k = 0; k < sizeof(k_calls) / sizeof(k_calls[0]); k++ )
                {
                    size_t const n = strlen(k_calls[k]);
                    if( p + n <= block->body_end && memcmp(text + p, k_calls[k], n) == 0 )
                    {
                        calls++;
                        break;
                    }
                }
            }
            else if( text[p] == '@' && head == 1 )
            {
                /* A label this one jumps to: its dialogue counts too. */
                size_t n = 0;
                while( p + 1 + n < block->body_end && quest_ident(text[p + 1 + n]) )
                    n++;
                for( int i = 0; i < scan->block_count && i < EDITOR_QUEST_TRIGGERS_MAX && tail < 16; i++ )
                {
                    if( !seen[i] && strcmp(scan->blocks[i].type, "label") == 0 && strlen(scan->blocks[i].subject) == n
                        && memcmp(scan->blocks[i].subject, text + p + 1, n) == 0 )
                    {
                        seen[i] = 1;
                        queue[tail++] = i;
                    }
                }
            }
        }
    }
    return calls;
}

static int
quest_name_compare(
    const void* a,
    const void* b)
{
    return strcmp((const char*)a, (const char*)b);
}

/* ---- the board ---------------------------------------------------------------------- */

void
Editor_QuestBoardInit(struct Editor_QuestBoard* board)
{
    assert(board);
    memset(board, 0, sizeof(*board));
    board->points = -1;
}

void
Editor_QuestBoardFree(struct Editor_QuestBoard* board)
{
    if( !board )
        return;
    ToriMem_Free(ToriMem_Editor(), board->ledger.rows);
    memset(board, 0, sizeof(*board));
    board->points = -1;
}

static struct Editor_QuestRow*
quest_row(
    struct Editor_QuestBoard* board,
    enum Editor_QuestPart part,
    enum Editor_QuestState state)
{
    struct Editor_QuestRow* row;

    if( board->row_count >= EDITOR_QUEST_ROWS_MAX )
        return NULL;
    row = &board->rows[board->row_count++];
    memset(row, 0, sizeof(*row));
    row->part = part;
    row->state = state;
    return row;
}

int
Editor_QuestLedgerParse(
    struct Editor_QuestLedger* ledger,
    const char* text,
    size_t length)
{
    size_t at = 0;
    const char* line;
    size_t line_length;
    int capacity = 0;

    assert(ledger);
    assert(text || length == 0);

    ToriMem_Free(ToriMem_Editor(), ledger->rows);
    memset(ledger, 0, sizeof(*ledger));
    if( !quest_line(text ? text : "", length, &at, &line, &line_length) || line_length != 15
        || memcmp(line, "quest-ledger-v1", 15) != 0 )
        return 0;
    ledger->present = 1;
    while( quest_line(text, length, &at, &line, &line_length) )
    {
        char cells[6][256];
        int cell_count = 0;

        if( line_length == 0 || (line_length >= 6 && memcmp(line, "index\t", 6) == 0) )
            continue;
        for( size_t start = 0; start <= line_length && cell_count < 6; )
        {
            size_t stop = start;
            /* The detail is the rest of the line. */
            while( stop < line_length && (line[stop] != '\t' || cell_count == 5) )
                stop++;
            quest_fmt(cells[cell_count], sizeof(cells[0]), "%.*s", (int)(stop - start), line + start);
            cell_count++;
            start = stop + 1;
        }
        while( cell_count < 6 )
            cells[cell_count++][0] = '\0';
        if( strcmp(cells[0], "SUMMARY") == 0 )
        {
            const char* p;
            ledger->summary_present = 1;
            quest_fmt(ledger->summary_verdict, sizeof(ledger->summary_verdict), "%s", cells[2]);
            if( strncmp(cells[4], "exit=", 5) == 0 )
                ledger->exit_code = atoi(cells[4] + 5);
            if( (p = strstr(cells[5], "pass=")) != NULL )
                ledger->pass = atoi(p + 5);
            if( (p = strstr(cells[5], "fail=")) != NULL )
                ledger->fail = atoi(p + 5);
            if( (p = strstr(cells[5], "blocked=")) != NULL )
                ledger->blocked = atoi(p + 8);
            continue;
        }
        if( ledger->count == capacity )
        {
            capacity = capacity ? capacity * 2 : 32;
            ledger->rows = ToriMem_Realloc(ToriMem_Editor(), ledger->rows, sizeof(ledger->rows[0]) * (size_t)capacity);
            assert(ledger->rows);
        }
        {
            struct Editor_QuestLedgerRow* row = &ledger->rows[ledger->count++];
            row->index = atoi(cells[0]);
            quest_fmt(row->step, sizeof(row->step), "%s", cells[1]);
            quest_fmt(row->verdict, sizeof(row->verdict), "%s", cells[2]);
            row->ticks = atoi(cells[3]);
            quest_fmt(row->shots, sizeof(row->shots), "%s", cells[4]);
            quest_fmt(row->detail, sizeof(row->detail), "%s", cells[5]);
            for( const char* p = row->shots; *p; )
            {
                const char* comma = strchr(p, ',');
                if( (comma ? comma : p + strlen(p)) > p )
                    ledger->shots++;
                if( !comma )
                    break;
                p = comma + 1;
            }
        }
    }
    return 1;
}

int
Editor_QuestGateParse(
    const char* text,
    size_t length,
    const char* id,
    char* out,
    size_t out_size)
{
    size_t at = 0;
    const char* line;
    size_t line_length;
    size_t const id_length = strlen(id);

    assert(text || length == 0);
    assert(id);
    assert(out);
    assert(out_size > 0);

    out[0] = '\0';
    while( quest_line(text ? text : "", length, &at, &line, &line_length) )
    {
        size_t p;
        size_t word;
        if( line_length == 0 || line[0] == '#' )
            continue;
        if( line_length <= id_length || memcmp(line, id, id_length) != 0 || line[id_length] != ' ' )
            continue;
        p = id_length;
        while( p < line_length && line[p] == ' ' )
            p++;
        word = p;
        while( p < line_length && line[p] != ' ' )
            p++;
        if( p == word )
            continue;
        quest_fmt(out, out_size, "%.*s", (int)(p - word), line + word);
        return 1;
    }
    return 0;
}

void
Editor_QuestCountTestRows(
    const char* lua,
    size_t length,
    int* out_rows,
    int* out_shots)
{
    static const char* const k_rows[] = { "t.exec(", "t.expect(", "t.check(", "t.step(", "t.blocked(" };
    static const char k_complete[] = "t.quest.expect_complete(";
    size_t at = 0;
    const char* line;
    size_t line_length;

    assert(lua || length == 0);
    assert(out_rows);
    assert(out_shots);

    *out_rows = 0;
    *out_shots = 0;
    while( quest_line(lua ? lua : "", length, &at, &line, &line_length) )
    {
        size_t code = line_length;
        /* A comment ends the line's code ("--" outside a string is rare
         * enough in these files to take as the comment). */
        for( size_t i = 0; i + 1 < line_length; i++ )
        {
            if( line[i] == '-' && line[i + 1] == '-' )
            {
                code = i;
                break;
            }
        }
        for( size_t i = 0; i < code; i++ )
        {
            if( i > 0 && (quest_ident(line[i - 1]) || line[i - 1] == '.') )
                continue;
            for( size_t k = 0; k < sizeof(k_rows) / sizeof(k_rows[0]); k++ )
            {
                size_t const n = strlen(k_rows[k]);
                if( i + n <= code && memcmp(line + i, k_rows[k], n) == 0 )
                {
                    (*out_rows)++;
                    if( k == 0 || k == 2 )
                        (*out_shots)++;
                }
            }
            /* quest.expect_complete writes four rows and the quest.scroll
             * photograph (docs/QUEST_AUTHORING.md). */
            if( i + sizeof(k_complete) - 1 <= code && memcmp(line + i, k_complete, sizeof(k_complete) - 1) == 0 )
            {
                *out_rows += 4;
                (*out_shots)++;
            }
        }
    }
}

void
Editor_QuestShotPath(
    const char* id,
    const char* name,
    char* out,
    size_t out_size)
{
    enum
    {
        STEM_MAX = 71 - 4,
        HASH_DIGITS = 8,
        TAIL = 8,
    };
    size_t const length = strlen(name);

    assert(id);
    assert(name);
    assert(out);

    if( length <= STEM_MAX )
    {
        quest_fmt(out, out_size, EDITOR_QUEST_LEDGER_DIR "/%s/shots/%s.png", id, name);
        return;
    }
    {
        /* gate.py shot_file_stem: the head, "~", FNV-1a 32 of the whole
         * name, "~", the last eight bytes. */
        uint32_t hash = 2166136261u;
        size_t const head = STEM_MAX - HASH_DIGITS - TAIL - 2;
        for( size_t i = 0; i < length; i++ )
        {
            hash ^= (unsigned char)name[i];
            hash *= 16777619u;
        }
        quest_fmt(out, out_size, EDITOR_QUEST_LEDGER_DIR "/%s/shots/%.*s~%08x~%s.png", id, (int)head, name,
            (unsigned)hash, name + length - TAIL);
    }
}

/** `^name = <int>` lines; the stage constants are those sharing the
 *  not_started constant's prefix (and not its points). */
static void
quest_read_constants(
    struct Editor_QuestBoard* board,
    const char* text,
    size_t length)
{
    size_t at = 0;
    const char* line;
    size_t line_length;
    size_t const prefix_length = strlen(board->prefix);

    while( quest_line(text, length, &at, &line, &line_length) )
    {
        char name[128];
        char value_text[32];
        size_t p = 0;
        size_t name_start;
        char* end;
        long value;

        while( p < line_length && (line[p] == ' ' || line[p] == '\t') )
            p++;
        if( p >= line_length || line[p] != '^' )
            continue;
        name_start = ++p;
        while( p < line_length && quest_ident(line[p]) )
            p++;
        quest_fmt(name, sizeof(name), "%.*s", (int)(p - name_start), line + name_start);
        while( p < line_length && line[p] == ' ' )
            p++;
        if( p >= line_length || line[p] != '=' )
            continue;
        p++;
        while( p < line_length && line[p] == ' ' )
            p++;
        quest_fmt(value_text, sizeof(value_text), "%.*s", (int)(line_length - p), line + p);
        value = strtol(value_text, &end, 10);
        if( end == value_text || *end != '\0' )
            continue;
        if( strncmp(name, board->prefix, prefix_length) != 0 )
            continue;
        if( strstr(name + prefix_length, "questpoints") )
        {
            board->points = (int)value;
            continue;
        }
        if( board->constant_count >= EDITOR_QUEST_CONSTANTS_MAX )
            continue;
        quest_fmt(board->constants[board->constant_count].name, sizeof(board->constants[0].name), "%s",
            name + prefix_length);
        board->constants[board->constant_count].value = (int)value;
        board->constant_count++;
    }
    /* By value: the stage order. */
    for( int i = 1; i < board->constant_count; i++ )
    {
        struct Editor_QuestConstant held = board->constants[i];
        int j = i - 1;
        while( j >= 0 && board->constants[j].value > held.value )
        {
            board->constants[j + 1] = board->constants[j];
            j--;
        }
        board->constants[j + 1] = held;
    }
}

static int
quest_constant(
    const struct Editor_QuestBoard* board,
    const char* name)
{
    for( int i = 0; i < board->constant_count; i++ )
    {
        if( strcmp(board->constants[i].name, name) == 0 )
            return i;
    }
    return -1;
}

/** The test file names stage `name`: `expect_stage("<name>")` or a row
 *  `quest.stage.<name>`. */
static int
quest_test_names_stage(
    const char* lua,
    const char* name)
{
    char probe[160];
    if( !lua )
        return 0;
    quest_fmt(probe, sizeof(probe), "expect_stage(\"%s\")", name);
    if( strstr(lua, probe) )
        return 1;
    quest_fmt(probe, sizeof(probe), "expect_stage('%s')", name);
    if( strstr(lua, probe) )
        return 1;
    quest_fmt(probe, sizeof(probe), "quest.stage.%s\"", name);
    return strstr(lua, probe) != NULL;
}

/** "opnpc1 cook" / "~proc" / "label x" for a block. */
static void
quest_block_name(
    const struct quest_block* b,
    char* out,
    size_t out_size)
{
    if( strcmp(b->type, "proc") == 0 )
        quest_fmt(out, out_size, "~%s", b->subject);
    else
        quest_fmt(out, out_size, "%s %s", b->type, b->subject);
}

static void
quest_block_where(
    const struct quest_scan* scan,
    const struct quest_block* b,
    struct Editor_QuestRow* row)
{
    const char* path = scan->files[b->file].path;
    quest_fmt(row->where, sizeof(row->where), "%s:%d", path, b->line);
    row->open = EDITOR_QUEST_OPEN_TRIGGERS;
    /* The Triggers panel's File mode is relative to server/scripts. */
    quest_fmt(row->open_file, sizeof(row->open_file), "%s",
        strncmp(path, "server/scripts/", 15) == 0 ? path + 15 : path);
}

/** The first block that uses `^<prefix><name>` as `use` (SETS / TESTS), -1. */
static int
quest_find_use(
    const struct Editor_QuestBoard* board,
    const struct quest_scan* scan,
    const char* name,
    int use,
    int* out_line)
{
    char constant[160];
    const char* short_var = board->var;

    if( !board->var[0] )
        return -1;
    /* "varp29_cookquest" is also written "cookquest" in older files. */
    if( (strncmp(board->var, "varp", 4) == 0 || strncmp(board->var, "varb", 4) == 0) && isdigit((unsigned char)board->var[4]) )
    {
        const char* under = strchr(board->var, '_');
        if( under )
            short_var = under + 1;
    }
    quest_fmt(constant, sizeof(constant), "%s%s", board->prefix, name);
    for( int i = 0; i < scan->block_count; i++ )
    {
        if( quest_block_use(scan, &scan->blocks[i], board->var, short_var, constant, out_line) & use )
            return i;
    }
    return -1;
}

int
Editor_QuestRead(
    struct Editor_QuestBoard* board,
    const char* repo_root,
    const char* content_root,
    const char* id,
    const struct Editor_QuestCatalog* catalog)
{
    char path[EDITOR_QUEST_PATH_MAX * 2];
    char quest_rel[EDITOR_QUEST_PATH_MAX];
    char* lua = NULL;
    size_t lua_length = 0;
    struct quest_scan scan;
    struct Editor_QuestRow* row;
    char not_started[EDITOR_QUEST_NAME_MAX] = "";
    char complete[EDITOR_QUEST_NAME_MAX] = "";

    assert(board);
    assert(repo_root);
    assert(content_root);
    assert(id);
    assert(Editor_QuestIdValid(id));

    Editor_QuestBoardFree(board);
    Editor_QuestBoardInit(board);
    memset(&scan, 0, sizeof(scan));
    quest_fmt(board->id, sizeof(board->id), "%s", id);

    /* QUEUE.tsv: the quest's directory. */
    {
        static const char* const k_want[] = { "quest_dir" };
        char values[1][EDITOR_QUEST_NAME_MAX];
        size_t length;
        char* text;
        quest_fmt(path, sizeof(path), "%s/test/quests/QUEUE.tsv", repo_root);
        text = quest_read(path, &length);
        if( text && quest_tsv_row(text, length, "test_id", id, k_want, 1, values) && values[0][0] )
        {
            board->queue_found = 1;
            quest_fmt(board->quest_dir, sizeof(board->quest_dir), "%s", values[0]);
        }
        ToriMem_Free(ToriMem_Editor(), text);
    }
    /* quest_inventory.tsv: the name, the var, the constants' prefix. */
    if( board->quest_dir[0] )
    {
        static const char* const k_want[] = { "human_name", "varp", "const_not_started", "const_complete" };
        char values[4][EDITOR_QUEST_NAME_MAX];
        size_t length;
        char* text;
        quest_fmt(path, sizeof(path), "%s/tools/quest_gate/quest_inventory.tsv", repo_root);
        text = quest_read(path, &length);
        if( text && quest_tsv_row(text, length, "dir", board->quest_dir, k_want, 4, values) )
        {
            const char* stage = values[2][0] == '^' ? values[2] + 1 : values[2];
            const char* tail = strstr(stage, "not_started");
            quest_fmt(board->display, sizeof(board->display), "%s", values[0]);
            quest_fmt(board->var, sizeof(board->var), "%s", values[1][0] == '%' ? values[1] + 1 : values[1]);
            if( tail && (size_t)(tail - stage) < sizeof(board->prefix) )
                quest_fmt(board->prefix, sizeof(board->prefix), "%.*s", (int)(tail - stage), stage);
            quest_fmt(not_started, sizeof(not_started), "not_started");
            {
                const char* done = values[3][0] == '^' ? values[3] + 1 : values[3];
                size_t const prefix_length = strlen(board->prefix);
                if( strncmp(done, board->prefix, prefix_length) == 0 )
                    done += prefix_length;
                quest_fmt(complete, sizeof(complete), "%s", done);
            }
        }
        ToriMem_Free(ToriMem_Editor(), text);
    }
    if( board->quest_dir[0] )
        quest_fmt(quest_rel, sizeof(quest_rel), "server/scripts/quests/%s", board->quest_dir);
    else
        quest_rel[0] = '\0';

    /* The constants. */
    if( quest_rel[0] )
    {
        size_t length;
        char* text;
        char rel[EDITOR_QUEST_PATH_MAX];
        quest_fmt(rel, sizeof(rel), "%s/configs/%s.constant", quest_rel, board->quest_dir);
        quest_fmt(path, sizeof(path), "%s/%s", content_root, rel);
        text = quest_read(path, &length);
        if( text )
        {
            quest_fmt(board->constant_file, sizeof(board->constant_file), "%s", rel);
            quest_read_constants(board, text, length);
        }
        ToriMem_Free(ToriMem_Editor(), text);
    }

    /* The test file. */
    quest_fmt(path, sizeof(path), "%s/test/quests/%s.lua", repo_root, id);
    lua = quest_read(path, &lua_length);
    if( lua )
    {
        board->test_present = 1;
        Editor_QuestCountTestRows(lua, lua_length, &board->test_rows, &board->test_shots);
    }

    /* The scripts: the catalog's trigger files, then the directory. */
    if( quest_rel[0] )
    {
        if( catalog && catalog->triggers )
        {
            struct Editor_QuestTrigger* rows =
                ToriMem_Alloc(ToriMem_Editor(), sizeof(*rows) * EDITOR_QUEST_TRIGGERS_MAX);
            char dir[EDITOR_QUEST_PATH_MAX + 2];
            int count;
            assert(rows);
            quest_fmt(dir, sizeof(dir), "%s/", quest_rel);
            count = catalog->triggers(catalog->user, dir, rows, EDITOR_QUEST_TRIGGERS_MAX);
            for( int i = 0; i < count; i++ )
                quest_scan_add_file(&scan, content_root, rows[i].file);
            ToriMem_Free(ToriMem_Editor(), rows);
        }
        {
            char dir_path[EDITOR_QUEST_PATH_MAX * 2];
            DIR* dir;
            quest_fmt(dir_path, sizeof(dir_path), "%s/%s/scripts", content_root, quest_rel);
            dir = opendir(dir_path);
            if( dir )
            {
                struct dirent* entry;
                char names[EDITOR_QUEST_FILES_MAX][256];
                int name_count = 0;
                while( (entry = readdir(dir)) != NULL && name_count < EDITOR_QUEST_FILES_MAX )
                {
                    size_t const n = strlen(entry->d_name);
                    if( n > 4 && strcmp(entry->d_name + n - 4, ".rs2") == 0 && n < sizeof(names[0]) )
                        quest_fmt(names[name_count++], sizeof(names[0]), "%s", entry->d_name);
                }
                closedir(dir);
                /* readdir's order is the file system's: sort, so the board
                 * reads the same on every machine. */
                qsort(names, (size_t)name_count, sizeof(names[0]), quest_name_compare);
                for( int i = 0; i < name_count; i++ )
                {
                    char rel[EDITOR_QUEST_PATH_MAX];
                    quest_fmt(rel, sizeof(rel), "%s/scripts/%s", quest_rel, names[i]);
                    quest_scan_add_file(&scan, content_root, rel);
                }
            }
        }
        quest_scan_blocks(&scan);
    }

    /* dbrow */
    row = quest_row(board, EDITOR_QUEST_PART_DBROW, EDITOR_QUEST_MISSING);
    if( row )
    {
        char summary[EDITOR_QUEST_TEXT_MAX] = "";
        if( !board->quest_dir[0] )
            quest_fmt(row->what, sizeof(row->what), "dbrow: %s has no QUEUE.tsv row", id);
        else if( !catalog || !catalog->record )
        {
            row->state = EDITOR_QUEST_UNKNOWN;
            quest_fmt(row->what, sizeof(row->what), "dbrow %s", board->quest_dir);
        }
        else if( catalog->record(catalog->user, TORIRSMAPED_CATALOG_DBROW, board->quest_dir, row->where,
                     sizeof(row->where), summary, sizeof(summary)) )
        {
            row->state = EDITOR_QUEST_OK;
            quest_fmt(row->what, sizeof(row->what), "dbrow %s%s%s", board->quest_dir, summary[0] ? " (" : "",
                summary[0] ? summary : "");
            if( summary[0] )
                strncat(row->what, ")", sizeof(row->what) - strlen(row->what) - 1);
        }
        else
            quest_fmt(row->what, sizeof(row->what), "dbrow %s", board->quest_dir);
        if( board->quest_dir[0] )
        {
            row->open = EDITOR_QUEST_OPEN_RECORD;
            row->open_kind = TORIRSMAPED_CATALOG_DBROW;
            quest_fmt(row->open_name, sizeof(row->open_name), "%s", board->quest_dir);
        }
    }

    /* var and constants */
    row = quest_row(board, EDITOR_QUEST_PART_VAR, EDITOR_QUEST_MISSING);
    if( row )
    {
        size_t at;
        quest_fmt(row->what, sizeof(row->what), "var %s", board->var[0] ? board->var : "(none in quest_inventory.tsv)");
        for( int i = 0; i < board->constant_count; i++ )
        {
            at = strlen(row->what);
            quest_fmt(row->what + at, sizeof(row->what) - at, " \xc2\xb7 %s %d", board->constants[i].name,
                board->constants[i].value);
        }
        if( board->var[0] && board->constant_file[0] && quest_constant(board, not_started) >= 0
            && quest_constant(board, complete) >= 0 )
            row->state = EDITOR_QUEST_OK;
        if( board->constant_file[0] )
            quest_fmt(row->where, sizeof(row->where), "%s", board->constant_file);
        if( board->var[0] )
        {
            row->open = EDITOR_QUEST_OPEN_VARS;
            quest_fmt(row->open_name, sizeof(row->open_name), "%s", board->var);
        }
    }

    /* start: the block that sets the first stage after not_started, up to
     * the op trigger that runs it. */
    row = quest_row(board, EDITOR_QUEST_PART_START, EDITOR_QUEST_MISSING);
    if( row )
    {
        int const from = quest_constant(board, not_started);
        int b = -1;
        int line = 0;
        quest_fmt(row->what, sizeof(row->what), "start: no trigger sets the first stage");
        if( from >= 0 && from + 1 < board->constant_count )
            b = quest_find_use(board, &scan, board->constants[from + 1].name, QUEST_USE_SETS, &line);
        if( b >= 0 )
        {
            unsigned char visited[EDITOR_QUEST_TRIGGERS_MAX] = { 0 };
            char name[EDITOR_QUEST_TEXT_MAX];
            int op = b;
            for( int hop = 0; hop < 4 && !quest_is_op(scan.blocks[op].type); hop++ )
            {
                int caller;
                if( op < EDITOR_QUEST_TRIGGERS_MAX )
                    visited[op] = 1;
                caller = quest_caller(&scan, &scan.blocks[op], visited);
                if( caller < 0 )
                    break;
                op = caller;
            }
            quest_block_name(&scan.blocks[op], name, sizeof(name));
            quest_block_where(&scan, &scan.blocks[op], row);
            row->state = EDITOR_QUEST_OK;
            if( strncmp(scan.blocks[op].type, "opnpc", 5) == 0 )
            {
                /* The npc builder's model, when it wrote this npc. */
                char model_path[EDITOR_QUEST_PATH_MAX * 2];
                size_t length;
                char* model;
                int nodes = 0;
                quest_fmt(model_path, sizeof(model_path), "%s/server/scripts/edited/%s/%s.interaction", content_root,
                    scan.blocks[op].subject, scan.blocks[op].subject);
                model = quest_read(model_path, &length);
                if( model )
                {
                    size_t at = 0;
                    const char* l;
                    size_t n;
                    while( quest_line(model, length, &at, &l, &n) )
                    {
                        if( n > 6 && memcmp(l, "[node,", 6) == 0 )
                            nodes++;
                    }
                    ToriMem_Free(ToriMem_Editor(), model);
                    quest_fmt(row->what, sizeof(row->what), "start: %s (npc builder, %d nodes)", name, nodes);
                    row->open = EDITOR_QUEST_OPEN_NPC_BUILDER;
                    quest_fmt(row->open_name, sizeof(row->open_name), "%s", scan.blocks[op].subject);
                }
                else
                    quest_fmt(row->what, sizeof(row->what), "start: %s (dialogue, %d calls)", name,
                        quest_dialogue_calls(&scan, op));
            }
            else
                quest_fmt(row->what, sizeof(row->what), "start: %s (dialogue, %d calls)", name,
                    quest_dialogue_calls(&scan, op));
        }
    }

    /* each stage between not_started and complete: the block that tests it */
    for( int i = 0; i < board->constant_count; i++ )
    {
        const char* name = board->constants[i].name;
        int line = 0;
        int b;
        if( strcmp(name, not_started) == 0 || strcmp(name, complete) == 0 )
            continue;
        row = quest_row(board, EDITOR_QUEST_PART_STAGE, EDITOR_QUEST_MISSING);
        if( !row )
            break;
        b = quest_find_use(board, &scan, name, QUEST_USE_TESTS, &line);
        if( b < 0 )
            b = quest_find_use(board, &scan, name, QUEST_USE_SETS, &line);
        if( b < 0 )
        {
            quest_fmt(row->what, sizeof(row->what), "stage %s: no trigger reads or sets it", name);
            continue;
        }
        {
            char block_name[EDITOR_QUEST_TEXT_MAX];
            quest_block_name(&scan.blocks[b], block_name, sizeof(block_name));
            quest_fmt(row->what, sizeof(row->what), "stage %s: %s", name, block_name);
            quest_block_where(&scan, &scan.blocks[b], row);
            quest_fmt(row->where, sizeof(row->where), "%s:%d", scan.files[scan.blocks[b].file].path, line);
            row->state = quest_test_names_stage(lua, name) ? EDITOR_QUEST_OK : EDITOR_QUEST_NO_TEST_ROW;
        }
    }

    /* complete */
    row = quest_row(board, EDITOR_QUEST_PART_COMPLETE, EDITOR_QUEST_MISSING);
    if( row )
    {
        int line = 0;
        int const b = complete[0] ? quest_find_use(board, &scan, complete, QUEST_USE_SETS, &line) : -1;
        quest_fmt(row->what, sizeof(row->what), "complete: no trigger sets %s%s", board->prefix,
            complete[0] ? complete : "complete");
        if( b >= 0 )
        {
            char name[EDITOR_QUEST_TEXT_MAX];
            quest_block_name(&scan.blocks[b], name, sizeof(name));
            quest_fmt(row->what, sizeof(row->what), "complete: %s", name);
            quest_block_where(&scan, &scan.blocks[b], row);
            quest_fmt(row->where, sizeof(row->where), "%s:%d", scan.files[scan.blocks[b].file].path, line);
            row->state = EDITOR_QUEST_OK;
        }
    }

    /* journal */
    row = quest_row(board, EDITOR_QUEST_PART_JOURNAL, EDITOR_QUEST_MISSING);
    if( row )
    {
        quest_fmt(row->what, sizeof(row->what), "journal: no [questjournal] block or *_journal proc");
        for( int i = 0; i < scan.block_count; i++ )
        {
            const struct quest_block* b = &scan.blocks[i];
            size_t const n = strlen(b->subject);
            if( strstr(b->type, "journal")
                || (strcmp(b->type, "proc") == 0 && n > 8 && strcmp(b->subject + n - 8, "_journal") == 0) )
            {
                const char* text = scan.files[b->file].text;
                size_t at = b->body;
                const char* l;
                size_t length;
                int lines = 0;
                while( at < b->body_end && quest_line(text, b->body_end, &at, &l, &length) )
                {
                    if( memchr(l, '"', length) )
                        lines++;
                }
                quest_fmt(row->what, sizeof(row->what), "journal lines (%d)", lines);
                quest_block_where(&scan, b, row);
                row->state = EDITOR_QUEST_OK;
                break;
            }
        }
    }

    /* the test, its ledger and the gate's verdict */
    {
        size_t length;
        char* text;
        quest_fmt(path, sizeof(path), "%s/" EDITOR_QUEST_LEDGER_DIR "/%s/ledger.tsv", repo_root, id);
        text = quest_read(path, &length);
        if( text && Editor_QuestLedgerParse(&board->ledger, text, length) )
            board->ledger.mtime = quest_mtime(path);
        ToriMem_Free(ToriMem_Editor(), text);
        quest_fmt(path, sizeof(path), "%s/" EDITOR_QUEST_GATE_DIR "/%s.gate", repo_root, id);
        text = quest_read(path, &length);
        if( text && Editor_QuestGateParse(text, length, id, board->gate_word, sizeof(board->gate_word)) )
            board->gate_mtime = quest_mtime(path);
        ToriMem_Free(ToriMem_Editor(), text);
    }
    if( !board->test_present )
        board->verdict = EDITOR_QUEST_MISSING;
    else if( !board->ledger.present )
        board->verdict = EDITOR_QUEST_NOT_RUN;
    else if( !board->gate_word[0] )
        board->verdict = EDITOR_QUEST_NOT_GATED;
    else if( board->gate_mtime < board->ledger.mtime )
        board->verdict = EDITOR_QUEST_STALE;
    else if( strcmp(board->gate_word, "green") == 0 )
        board->verdict = EDITOR_QUEST_GREEN;
    else if( strcmp(board->gate_word, "RED") == 0 )
        board->verdict = EDITOR_QUEST_RED;
    else if( strcmp(board->gate_word, "blocked") == 0 )
        board->verdict = EDITOR_QUEST_BLOCKED;
    else
        board->verdict = EDITOR_QUEST_NOT_GATED;
    row = quest_row(board, EDITOR_QUEST_PART_TEST, board->verdict);
    if( row )
    {
        size_t at;
        quest_fmt(row->what, sizeof(row->what), "test/quests/%s.lua", id);
        if( board->test_present )
        {
            at = strlen(row->what);
            quest_fmt(row->what + at, sizeof(row->what) - at, " \xc2\xb7 %d rows \xc2\xb7 %d shots", board->test_rows,
                board->test_shots);
        }
        at = strlen(row->what);
        if( board->ledger.present && board->ledger.summary_present )
            quest_fmt(row->what + at, sizeof(row->what) - at, " \xc2\xb7 ledger %s %d pass %d fail%s",
                board->ledger.summary_verdict, board->ledger.pass, board->ledger.fail,
                board->ledger.blocked ? " (blocked)" : "");
        else if( board->ledger.present )
            quest_fmt(row->what + at, sizeof(row->what) - at, " \xc2\xb7 ledger with no SUMMARY row");
        at = strlen(row->what);
        if( board->gate_word[0] )
            quest_fmt(row->what + at, sizeof(row->what) - at, " \xc2\xb7 gate: %s", board->gate_word);
        if( board->test_present )
        {
            quest_fmt(row->where, sizeof(row->where), "test/quests/%s.lua", id);
            row->open = EDITOR_QUEST_OPEN_CODE;
            quest_fmt(row->open_file, sizeof(row->open_file), "test/quests/%s.lua", id);
        }
    }

    ToriMem_Free(ToriMem_Editor(), lua);
    quest_scan_free(&scan);
    return board->row_count;
}

/* ---- prerequisites --------------------------------------------------------------- */

int
Editor_QuestCanScaffold(
    const struct Editor_QuestBoard* board,
    const char* repo_root,
    const char* helper_root,
    char* why,
    size_t why_size)
{
    char path[EDITOR_QUEST_PATH_MAX * 2];

    assert(board);
    assert(repo_root);
    assert(helper_root);
    assert(why);

    why[0] = '\0';
    quest_fmt(path, sizeof(path), "%s/tools/quest_gate/new_quest.py", repo_root);
    if( !quest_exists(path) )
    {
        quest_fmt(why, why_size, "no tools/quest_gate/new_quest.py under %s", repo_root);
        return 0;
    }
    if( !board->queue_found )
    {
        quest_fmt(why, why_size, "%s has no test/quests/QUEUE.tsv row for new_quest.py to scaffold from", board->id);
        return 0;
    }
    if( board->test_present )
    {
        quest_fmt(why, why_size, "test/quests/%s.lua exists; new_quest.py refuses to clobber it", board->id);
        return 0;
    }
    if( !helper_root[0] || !quest_exists(helper_root) )
    {
        quest_fmt(why, why_size, "the Quest Helper guides are not at '%s' (TORIRS_QUEST_HELPER_ROOT)", helper_root);
        return 0;
    }
    return 1;
}

int
Editor_QuestCanGate(
    const struct Editor_QuestBoard* board,
    const char* repo_root,
    const char* content_root,
    char* why,
    size_t why_size)
{
    static const char* const k_tools[] = { "tools/quest_gate/run.py", "tools/quest_gate/gate.py" };
    char path[EDITOR_QUEST_PATH_MAX * 2];

    assert(board);
    assert(repo_root);
    assert(content_root);
    assert(why);

    why[0] = '\0';
    for( size_t i = 0; i < sizeof(k_tools) / sizeof(k_tools[0]); i++ )
    {
        quest_fmt(path, sizeof(path), "%s/%s", repo_root, k_tools[i]);
        if( !quest_exists(path) )
        {
            quest_fmt(why, why_size, "no %s under %s", k_tools[i], repo_root);
            return 0;
        }
    }
    if( !board->test_present )
    {
        quest_fmt(why, why_size, "no test/quests/%s.lua to run (Scaffold test writes one)", board->id);
        return 0;
    }
    quest_fmt(path, sizeof(path), "%s/server/scripts", content_root);
    if( !quest_exists(path) )
    {
        quest_fmt(why, why_size, "the content tree's scripts are not at %s (is OSRS-Content checked out?)", path);
        return 0;
    }
    quest_fmt(path, sizeof(path), "%s/src/torirs_questtest", repo_root);
    if( !quest_exists(path) )
    {
        quest_fmt(why, why_size, "no src/torirs_questtest for run.py --no-build to drive (build it first)");
        return 0;
    }
    return 1;
}
