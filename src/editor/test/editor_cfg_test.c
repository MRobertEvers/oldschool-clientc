/**
 * The config text codec (editor_cfg.h) and the CFG document's commands.
 *
 *   editor_cfg_test <content-dir>
 *
 * The round trip is the contract: every `configs/all.*` and every
 * `server/scripts/.../configs/<file>.{loc,npc,obj,varp,varbit,enum,struct,param,inv}`
 * indexes into contiguous blocks, and every block (and the preamble) parses
 * into lines and emits back from the parsed parts byte for byte. The files
 * of those types elsewhere under server/scripts and every file under a
 * lane's `ported/<lane>/configs/` are held to the same rule and counted
 * apart, so the brief's number stays the brief's.
 *
 * On the real records, an edit and its inverse leave the block as it was and
 * a block put back into its file changes no byte outside the block. Then the
 * edit operations on small texts, and CFG_FIELD / CFG_BLOCK through
 * Editor_CmdApply and the undo stack.
 *
 * Read-only over the content tree.
 */

#include "editor/editor_cfg.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "mem/torirs_mem.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static char*
slurp(
    const char* path,
    size_t* out_size)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    data = malloc((size_t)size + 1);
    if( !data )
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

/* ---- the round trip over the tree ---------------------------------------- */

struct Totals
{
    int files;
    long blocks;
    long lines;
    long long bytes;
    int failed_files;
    long edits;
    int failed_edits;
};

static char* g_emit;
static size_t g_emit_capacity;

static char*
emit_buffer(size_t size)
{
    if( size + 1 > g_emit_capacity )
    {
        g_emit_capacity = size + 1 + size / 2;
        g_emit = realloc(g_emit, g_emit_capacity);
        if( !g_emit )
        {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    return g_emit;
}

/** Parse [start, end) as a block and emit it from its parts. */
static int
round_trip_span(
    struct Editor_CfgBlock* block,
    const char* text,
    size_t start,
    size_t end,
    long* out_lines)
{
    size_t emitted;
    char* out;

    Editor_CfgBlockSet(block, text + start, end - start);
    emitted = Editor_CfgEmitLines(block->text, block->lines, block->line_count, NULL);
    out = emit_buffer(emitted);
    if( Editor_CfgEmitLines(block->text, block->lines, block->line_count, out) != emitted )
        return 0;
    *out_lines += block->line_count;
    return emitted == end - start && memcmp(out, text + start, emitted) == 0;
}

/**
 * On a sample of real records: set the first field to a probe and back,
 * remove it with its line and insert it back, and put the edited block into
 * the file — only the block's bytes may differ.
 */
static void
edit_probe(
    struct Totals* totals,
    const char* path,
    const char* text,
    size_t length,
    const struct Editor_CfgFile* file,
    int index)
{
    const struct Editor_CfgBlockRef* ref = &file->blocks[index];
    struct Editor_CfgBlock block;
    char key[EDITOR_CMD_NAME_MAX];
    char value[EDITOR_CMD_VALUE_MAX];
    char name[EDITOR_CMD_NAME_MAX];
    int field = -1;
    int ok = 1;

    if( ref->name_length == 0 || ref->name_length >= sizeof(name) )
        return;
    memcpy(name, text + ref->name_start, ref->name_length);
    name[ref->name_length] = '\0';

    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, text + ref->start, ref->end - ref->start);
    for( int i = 0; i < block.line_count; i++ )
    {
        if( block.lines[i].kind == EDITOR_CFG_LINE_FIELD && block.lines[i].part > 0
            && block.lines[i].part < sizeof(key) )
        {
            size_t value_length;
            const char* current = Editor_CfgBlockValue(&block, i, &value_length);

            if( value_length >= sizeof(value) )
                continue;
            memcpy(key, block.text + block.lines[i].start, block.lines[i].part);
            key[block.lines[i].part] = '\0';
            {
                char parsed[EDITOR_CMD_NAME_MAX];
                int occurrence_unused;
                int line_unused;

                if( !Editor_CfgFieldSpecParse(
                        key, parsed, sizeof(parsed), &occurrence_unused, &line_unused)
                    || strcmp(parsed, key) != 0 )
                    continue;
            }
            memcpy(value, current, value_length);
            value[value_length] = '\0';
            field = i;
            break;
        }
    }
    if( field < 0 )
    {
        Editor_CfgBlockFree(&block);
        return;
    }

    totals->edits++;
    {
        int occurrence = 0;

        for( int i = 0; i < field; i++ )
        {
            if( block.lines[i].kind == EDITOR_CFG_LINE_FIELD && block.lines[i].part == strlen(key)
                && memcmp(block.text + block.lines[i].start, key, strlen(key)) == 0 )
                occurrence++;
        }
        ok &= Editor_CfgBlockFieldSet(&block, key, occurrence, "editor_cfg_probe");
        ok &= Editor_CfgBlockFieldIs(&block, key, occurrence, "editor_cfg_probe");

        /* The edited block back into its file: only its own bytes move. */
        {
            size_t merged_length;
            char* merged = Editor_CfgFileReplaceBlock(
                text, length, name, block.text, block.length, &merged_length);
            size_t start;
            size_t end;

            /* A "[name] tail" header has no exact extent; it is appended, which
             * is not this check. */
            if( Editor_CfgBlockExtent(text, length, name, &start, &end) )
            {
                ok &= merged_length == length - (end - start) + block.length;
                ok &= memcmp(merged, text, start) == 0;
                ok &= memcmp(merged + start + block.length, text + end, length - end) == 0;
            }
            ToriMem_Free(ToriMem_Editor(), merged);
        }

        ok &= Editor_CfgBlockFieldSet(&block, key, occurrence, value);
        ok &= Editor_CfgBlockFieldRemove(&block, key, occurrence, field);
        ok &= Editor_CfgBlockFieldInsert(&block, key, occurrence, field, value);
        ok &= block.length == ref->end - ref->start
              && memcmp(block.text, text + ref->start, block.length) == 0;
    }
    if( !ok )
    {
        totals->failed_edits++;
        if( totals->failed_edits <= 5 )
            fprintf(stderr, "FAIL: edit and undo not byte-exact: %s [%s] %s\n", path, name, key);
    }
    Editor_CfgBlockFree(&block);
}

static void
round_trip_file(
    struct Totals* totals,
    const char* path)
{
    size_t length = 0;
    char* text = slurp(path, &length);
    struct Editor_CfgFile file;
    struct Editor_CfgBlock block;
    int ok = 1;
    long lines = 0;

    if( !text )
    {
        fprintf(stderr, "FAIL: cannot read %s\n", path);
        totals->failed_files++;
        return;
    }

    Editor_CfgFileIndex(&file, text, length);
    Editor_CfgBlockInit(&block);

    /* Contiguous: preamble, then each block up to the next. */
    if( file.block_count > 0 )
    {
        ok &= file.preamble_end == file.blocks[0].start;
        for( int i = 0; i + 1 < file.block_count; i++ )
            ok &= file.blocks[i].end == file.blocks[i + 1].start;
        ok &= file.blocks[file.block_count - 1].end == length;
    }
    else
        ok &= file.preamble_end == length;

    ok &= round_trip_span(&block, text, 0, file.preamble_end, &lines);
    for( int i = 0; i < file.block_count && ok; i++ )
        ok &= round_trip_span(&block, text, file.blocks[i].start, file.blocks[i].end, &lines);

    /* The one-pass extent agrees with the index (first block of the name). */
    if( ok && file.block_count > 0 )
    {
        int probes[3] = { 0, file.block_count / 2, file.block_count - 1 };

        for( int p = 0; p < 3; p++ )
        {
            const struct Editor_CfgBlockRef* ref = &file.blocks[probes[p]];
            char name[256];
            size_t start;
            size_t end;
            int found;

            if( ref->name_length == 0 || ref->name_length >= sizeof(name) )
                continue;
            memcpy(name, text + ref->name_start, ref->name_length);
            name[ref->name_length] = '\0';
            found = Editor_CfgFileFind(&file, name);
            if( found < 0 )
                continue; /* "[name] tail": no exact header, by design */
            ok &= Editor_CfgBlockExtent(text, length, name, &start, &end) == 1;
            ok &= start == file.blocks[found].start && end == file.blocks[found].end;
        }
    }

    if( ok )
    {
        int stride = file.block_count > 400 ? file.block_count / 200 : 1;

        for( int i = 0; i < file.block_count; i += stride )
            edit_probe(totals, path, text, length, &file, i);
    }

    totals->files++;
    totals->blocks += file.block_count;
    totals->lines += lines;
    totals->bytes += (long long)length;
    if( !ok )
    {
        totals->failed_files++;
        if( totals->failed_files <= 10 )
            fprintf(stderr, "FAIL: not byte-exact: %s\n", path);
    }
    Editor_CfgBlockFree(&block);
    Editor_CfgFileFree(&file);
    free(text);
}

static int
has_extension(const char* name)
{
    static const char* const types[] = { "loc",  "npc",    "obj",   "varp", "varbit",
                                         "enum", "struct", "param", "inv" };
    const char* dot = strrchr(name, '.');

    if( !dot )
        return 0;
    for( size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++ )
    {
        if( strcmp(dot + 1, types[i]) == 0 )
            return 1;
    }
    return 0;
}

static int
compare_names(
    const void* a,
    const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

/** Sorted entry names of `dir` (no dot-names). Caller frees. */
static char**
list_dir(
    const char* dir,
    int* out_count)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    char** names = NULL;
    int count = 0;
    int capacity = 0;

    *out_count = 0;
    if( !handle )
        return NULL;
    while( (entry = readdir(handle)) != NULL )
    {
        if( entry->d_name[0] == '.' )
            continue;
        if( count == capacity )
        {
            capacity = capacity ? capacity * 2 : 64;
            names = realloc(names, sizeof(*names) * (size_t)capacity);
        }
        names[count++] = strdup(entry->d_name);
    }
    closedir(handle);
    qsort(names, (size_t)count, sizeof(*names), compare_names);
    *out_count = count;
    return names;
}

static void
free_list(
    char** names,
    int count)
{
    for( int i = 0; i < count; i++ )
        free(names[i]);
    free(names);
}

static int
is_dir(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/** server/scripts: typed files under a `configs` directory (any depth below
 *  it) into `in_configs`, typed files anywhere else into `elsewhere`. */
static void
walk_scripts(
    const char* dir,
    int under_configs,
    struct Totals* in_configs,
    struct Totals* elsewhere)
{
    int count;
    char** names = list_dir(dir, &count);

    for( int i = 0; i < count; i++ )
    {
        char path[4096];

        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if( is_dir(path) )
            walk_scripts(
                path, under_configs || strcmp(names[i], "configs") == 0, in_configs, elsewhere);
        else if( has_extension(names[i]) )
            round_trip_file(under_configs ? in_configs : elsewhere, path);
    }
    free_list(names, count);
}

static void
walk_all(
    const char* dir,
    struct Totals* totals)
{
    int count;
    char** names = list_dir(dir, &count);

    for( int i = 0; i < count; i++ )
    {
        char path[4096];

        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if( is_dir(path) )
            walk_all(path, totals);
        else
            round_trip_file(totals, path);
    }
    free_list(names, count);
}

static void
report(
    const char* what,
    const struct Totals* totals)
{
    printf(
        "  %-34s %5d files %8ld blocks %9ld lines %11lld bytes, %d not byte-exact; "
        "%ld edit+undo probes, %d failed\n",
        what,
        totals->files,
        totals->blocks,
        totals->lines,
        totals->bytes,
        totals->failed_files,
        totals->edits,
        totals->failed_edits);
}

static void
test_tree(const char* content_dir)
{
    struct Totals cache = { 0 };
    struct Totals server = { 0 };
    struct Totals elsewhere = { 0 };
    struct Totals lanes = { 0 };
    char path[4096];
    int count;
    char** names;
    clock_t began = clock();

    snprintf(path, sizeof(path), "%s/configs", content_dir);
    names = list_dir(path, &count);
    for( int i = 0; i < count; i++ )
    {
        char file[4096];

        if( strncmp(names[i], "all.", 4) != 0 )
            continue;
        snprintf(file, sizeof(file), "%s/%s", path, names[i]);
        round_trip_file(&cache, file);
    }
    free_list(names, count);

    snprintf(path, sizeof(path), "%s/server/scripts", content_dir);
    walk_scripts(path, 0, &server, &elsewhere);

    snprintf(path, sizeof(path), "%s/ported", content_dir);
    names = list_dir(path, &count);
    for( int i = 0; i < count; i++ )
    {
        char lane[4096];

        snprintf(lane, sizeof(lane), "%s/%s/configs", path, names[i]);
        if( is_dir(lane) )
            walk_all(lane, &lanes);
    }
    free_list(names, count);

    printf("round trip (%.1f s):\n", (double)(clock() - began) / CLOCKS_PER_SEC);
    report("configs/all.*", &cache);
    report("server/scripts/**/configs/*.<type>", &server);
    report("server/scripts typed, not configs/", &elsewhere);
    report("ported/*/configs/*", &lanes);
    printf(
        "  brief set: %d files (%d configs/all.* + %d server overlays), %d not byte-exact\n",
        cache.files + server.files,
        cache.files,
        server.files,
        cache.failed_files + server.failed_files);

    check(cache.files >= 40, "every configs/all.* file was read");
    check(server.files >= 1000, "every server overlay was read");
    check(cache.failed_files == 0, "configs/all.* round-trip byte-exact");
    check(server.failed_files == 0, "server overlays round-trip byte-exact");
    check(elsewhere.failed_files == 0, "typed files outside configs/ round-trip byte-exact");
    check(lanes.failed_files == 0, "lane configs round-trip byte-exact");
    check(
        cache.failed_edits + server.failed_edits + elsewhere.failed_edits + lanes.failed_edits == 0,
        "an edit and its undo leave every probed record byte-exact");
    check(cache.edits > 1000, "the edit probe ran on real records");
}

/* ---- the operations ------------------------------------------------------ */

static void
set_block(
    struct Editor_CfgBlock* block,
    const char* text)
{
    Editor_CfgBlockSet(block, text, strlen(text));
}

static int
block_is(
    const struct Editor_CfgBlock* block,
    const char* text)
{
    return block->length == strlen(text) && memcmp(block->text, text, block->length) == 0;
}

static void
test_specs(void)
{
    char key[EDITOR_CMD_NAME_MAX];
    char spec[EDITOR_CMD_NAME_MAX];
    int occurrence;
    int line;

    check(
        Editor_CfgFieldSpecParse("name", key, sizeof(key), &occurrence, &line)
            && strcmp(key, "name") == 0 && occurrence == 0 && line == -1,
        "a plain spec is occurrence 0, no line");
    check(
        Editor_CfgFieldSpecParse("val#2", key, sizeof(key), &occurrence, &line)
            && strcmp(key, "val") == 0 && occurrence == 2 && line == -1,
        "key#N names an occurrence");
    check(
        Editor_CfgFieldSpecParse("op1@3", key, sizeof(key), &occurrence, &line)
            && strcmp(key, "op1") == 0 && occurrence == 0 && line == 3,
        "key@L names a line");
    check(
        Editor_CfgFieldSpecParse("data#1@7", key, sizeof(key), &occurrence, &line)
            && occurrence == 1 && line == 7,
        "key#N@L names both");
    check(!Editor_CfgFieldSpecParse("", key, sizeof(key), &occurrence, &line), "empty key refused");
    check(!Editor_CfgFieldSpecParse("a@1#2", key, sizeof(key), &occurrence, &line),
        "line before occurrence refused");
    check(!Editor_CfgFieldSpecParse("[x", key, sizeof(key), &occurrence, &line),
        "a key that reads as a header refused");
    check(!Editor_CfgFieldSpecParse("//x", key, sizeof(key), &occurrence, &line),
        "a key that reads as a comment refused");
    check(!Editor_CfgFieldSpecParse("a#", key, sizeof(key), &occurrence, &line),
        "an empty occurrence refused");
    check(!Editor_CfgFieldSpecParse("a#x", key, sizeof(key), &occurrence, &line),
        "a non-numeric occurrence refused");
    check(
        Editor_CfgFieldSpecFormat("data", 1, 7, spec, sizeof(spec)) && strcmp(spec, "data#1@7") == 0,
        "format writes both");
    check(
        Editor_CfgFieldSpecFormat("name", 0, -1, spec, sizeof(spec)) && strcmp(spec, "name") == 0,
        "format leaves the defaults out");
}

static void
test_lines(void)
{
    const char* text = "[a] tail\r\nkey=v=w\n  // note\n\t \nodd\n[b";
    struct Editor_CfgBlock block;
    char out[128];
    size_t length;

    Editor_CfgBlockInit(&block);
    set_block(&block, text);
    check(block.line_count == 6, "six lines");
    check(block.lines[0].kind == EDITOR_CFG_LINE_HEADER && block.lines[0].closed
              && block.lines[0].part == 1 && block.lines[0].eol == EDITOR_CFG_EOL_CRLF,
        "a header with a tail and CRLF");
    check(block.lines[1].kind == EDITOR_CFG_LINE_FIELD && block.lines[1].part == 3,
        "the key ends at the first '='");
    check(block.lines[2].kind == EDITOR_CFG_LINE_COMMENT, "an indented comment");
    check(block.lines[3].kind == EDITOR_CFG_LINE_BLANK, "a blank line of tabs and spaces");
    check(block.lines[4].kind == EDITOR_CFG_LINE_OTHER, "a line with no '=' is carried");
    check(block.lines[5].kind == EDITOR_CFG_LINE_HEADER && !block.lines[5].closed
              && block.lines[5].eol == EDITOR_CFG_EOL_NONE,
        "an unclosed header at EOF with no ending");
    length = Editor_CfgEmitLines(block.text, block.lines, block.line_count, out);
    check(length == strlen(text) && memcmp(out, text, length) == 0, "emits back exactly");
    {
        size_t value_length;
        const char* value = Editor_CfgBlockValue(&block, 1, &value_length);
        check(value_length == 3 && memcmp(value, "v=w", 3) == 0, "the value keeps later '='");
    }
    Editor_CfgBlockFree(&block);
}

static void
test_edits(void)
{
    struct Editor_CfgBlock block;

    Editor_CfgBlockInit(&block);

    /* Set: only the value moves. */
    set_block(&block, "[door]\nname=Door\nop1=Open\n\n");
    check(Editor_CfgBlockFieldSet(&block, "name", 0, "Gate"), "set an existing field");
    check(block_is(&block, "[door]\nname=Gate\nop1=Open\n\n"), "set rewrites the value only");
    check(!Editor_CfgBlockFieldSet(&block, "desc", 0, "x"), "set of an absent field refused");
    check(!Editor_CfgBlockFieldSet(&block, "name", 0, "a\nb"), "a value with a break refused");

    /* Insert: after the last field, never after the next record's comment. */
    set_block(&block, "[a]\nx=1\n\n// about b\n");
    check(Editor_CfgBlockFieldInsert(&block, "y", 0, -1, "2"), "insert a new key");
    check(block_is(&block, "[a]\nx=1\ny=2\n\n// about b\n"),
        "a new key goes after the last field, above the trailing comment");
    check(Editor_CfgBlockFieldRemove(&block, "y", 0, -1), "remove it");
    check(block_is(&block, "[a]\nx=1\n\n// about b\n"), "remove takes back exactly that");

    /* Repeated keys by occurrence. */
    set_block(&block, "[e]\nval=1,a\nval=2,b\ndefault=0\n");
    check(Editor_CfgBlockFieldCount(&block, "val") == 2, "two occurrences");
    check(Editor_CfgBlockFieldInsert(&block, "val", 2, -1, "3,c"), "append a third");
    check(block_is(&block, "[e]\nval=1,a\nval=2,b\nval=3,c\ndefault=0\n"),
        "a new occurrence follows the key's last one");
    check(Editor_CfgBlockFieldSet(&block, "val", 1, "2,B"), "set the second");
    check(Editor_CfgBlockFieldIs(&block, "val", 1, "2,B"), "the second reads back");
    check(Editor_CfgBlockFieldInsert(&block, "val", 0, -1, "0,z"), "insert before the first");
    check(block_is(&block, "[e]\nval=0,z\nval=1,a\nval=2,B\nval=3,c\ndefault=0\n"),
        "occurrence 0 goes before the current first");
    check(!Editor_CfgBlockFieldInsert(&block, "val", 9, -1, "x"), "an occurrence past count refused");

    /* Removal with its line, then the inverse puts it back in place. */
    set_block(&block, "[r]\nfirst=1\nmiddle=2\nlast=3\n");
    {
        int line = Editor_CfgBlockFieldFind(&block, "middle", 0);

        check(line == 2, "middle is line 2");
        check(!Editor_CfgBlockFieldRemove(&block, "middle", 0, 3), "a stale line refused");
        check(Editor_CfgBlockFieldRemove(&block, "middle", 0, line), "remove at its line");
        check(Editor_CfgBlockFieldInsert(&block, "middle", 0, line, "2"), "insert at that line");
        check(block_is(&block, "[r]\nfirst=1\nmiddle=2\nlast=3\n"), "back in place, byte-exact");
        check(!Editor_CfgBlockFieldInsert(&block, "x", 0, 0, "1"), "a line on the header refused");
    }

    /* CRLF and a last line with no ending. */
    set_block(&block, "[c]\r\nx=1\r\n");
    check(Editor_CfgBlockFieldInsert(&block, "y", 0, -1, "2"), "insert into CRLF");
    check(block_is(&block, "[c]\r\nx=1\r\ny=2\r\n"), "the new line takes the header's ending");
    set_block(&block, "[n]\nx=1");
    check(Editor_CfgBlockFieldInsert(&block, "y", 0, -1, "2"), "insert after a last line with none");
    check(block_is(&block, "[n]\nx=1\ny=2"), "the ending goes between, the new last has none");
    check(Editor_CfgBlockFieldRemove(&block, "y", 0, -1), "remove the last line");
    check(block_is(&block, "[n]\nx=1"), "the ending before it goes with it");
    check(Editor_CfgBlockFieldRemove(&block, "x", 0, -1), "remove the only field");
    check(block_is(&block, "[n]"), "header alone, still no ending");

    /* Rename keeps the tail; header add/remove keep comments and blanks. */
    set_block(&block, "[old]\nx=1\n");
    check(Editor_CfgBlockRename(&block, "new"), "rename");
    check(block_is(&block, "[new]\nx=1\n"), "only the name moved");
    check(!Editor_CfgBlockRename(&block, "a]b"), "a name with ']' refused");
    check(!Editor_CfgBlockRemoveHeader(&block, "new"), "a header with fields stays");
    set_block(&block, "[gone]\n\n// about the next\n");
    check(Editor_CfgBlockRemoveHeader(&block, "gone"), "remove a header-only record");
    check(block_is(&block, "\n// about the next\n"), "the separator and comment stay");
    check(!Editor_CfgBlockRemoveHeader(&block, "gone"), "nothing left to remove");
    check(Editor_CfgBlockAddHeader(&block, "gone"), "add it back");
    check(block_is(&block, "[gone]\n\n// about the next\n"), "add restores the bytes");
    check(!Editor_CfgBlockAddHeader(&block, "twice"), "a second header refused");
    set_block(&block, "");
    check(Editor_CfgBlockAddHeader(&block, "fresh"), "add into an empty document");
    check(block_is(&block, "[fresh]\n"), "an empty document becomes the header");
    check(Editor_CfgBlockRemoveHeader(&block, "fresh"), "and back");
    check(block.length == 0, "empty again");

    Editor_CfgBlockFree(&block);
}

static void
test_file(void)
{
    const char* text = "// head\n\n[a]\nx=1\n\n[b]\ny=2\n";
    struct Editor_CfgFile file;
    size_t start;
    size_t end;
    size_t length;
    char* merged;

    Editor_CfgFileIndex(&file, text, strlen(text));
    check(file.block_count == 2 && file.preamble_end == 9, "two blocks after a preamble");
    check(Editor_CfgFileFind(&file, "b") == 1, "find by name");
    check(Editor_CfgFileFind(&file, "c") == -1, "a missing name is -1");
    check(Editor_CfgBlockExtent(text, strlen(text), "a", &start, &end) && start == 9 && end == 18,
        "a block runs to the next header");
    Editor_CfgFileFree(&file);

    merged = Editor_CfgFileReplaceBlock(text, strlen(text), "a", "[a]\nx=9\n\n", 9, &length);
    check(strcmp(merged, "// head\n\n[a]\nx=9\n\n[b]\ny=2\n") == 0, "replace one block in place");
    ToriMem_Free(ToriMem_Editor(), merged);
    merged = Editor_CfgFileReplaceBlock(text, strlen(text), "c", "[c]\n", 4, &length);
    check(strcmp(merged, "// head\n\n[a]\nx=1\n\n[b]\ny=2\n\n[c]\n") == 0,
        "a new record is appended after a blank line");
    ToriMem_Free(ToriMem_Editor(), merged);
    merged = Editor_CfgFileReplaceBlock("[a]\nx=1", 7, "c", "[c]\n", 4, &length);
    check(strcmp(merged, "[a]\nx=1\n\n[c]\n") == 0, "appending to a file with no final newline");
    ToriMem_Free(ToriMem_Editor(), merged);
    merged = Editor_CfgFileReplaceBlock(text, strlen(text), "a", "", 0, &length);
    check(strcmp(merged, "// head\n\n[b]\ny=2\n") == 0, "an empty block removes the record");
    ToriMem_Free(ToriMem_Editor(), merged);
}

/* ---- CFG commands through the document and the undo stack ---------------- */

static struct Editor_Doc g_doc;

static struct Editor_TextDoc*
open_cfg(
    const char* key,
    const char* text)
{
    struct Editor_DocHandle handle;

    if( !Editor_DocOpen(&g_doc, EDITOR_DOC_CFG, key, &handle) || !handle.text )
        return NULL;
    Editor_TextDocSet(handle.text, text, strlen(text));
    return handle.text;
}

static void
field_cmd(
    struct Editor_Cmd* command,
    const char* doc_key,
    const char* spec,
    const char* before,
    const char* after)
{
    Editor_CmdInit(command, EDITOR_DOC_CFG, doc_key, EDITOR_CMD_CFG_FIELD);
    snprintf(command->u.cfg_field.key, sizeof(command->u.cfg_field.key), "%s", spec);
    snprintf(command->u.cfg_field.before, sizeof(command->u.cfg_field.before), "%s", before);
    snprintf(command->u.cfg_field.after, sizeof(command->u.cfg_field.after), "%s", after);
    command->has_before = before[0] != '\0';
    command->has_after = after[0] != '\0';
}

static int
apply_push(
    struct Editor_UndoStack* undo,
    const struct Editor_Cmd* command)
{
    if( !Editor_CmdApply(&g_doc, command, EDITOR_CMD_FORWARD) )
        return 0;
    Editor_UndoPush(undo, command);
    return 1;
}

static void
test_commands(void)
{
    const char* original = "[door_a]\nname=Door\nop1=Open\nop2=Close\n\n// next\n";
    struct Editor_UndoStack undo;
    struct Editor_TextDoc* text;
    struct Editor_Cmd command;

    memset(&g_doc, 0, sizeof(g_doc));
    Editor_DocInit(&g_doc, NULL);
    Editor_UndoInit(&undo);

    text = open_cfg("loc:door_a", original);
    check(text != NULL, "a CFG document opens");
    if( !text )
        return;

    field_cmd(&command, "loc:door_a", "name", "Door", "Gate");
    check(apply_push(&undo, &command), "CFG_FIELD set applies");
    check(strcmp(text->text, "[door_a]\nname=Gate\nop1=Open\nop2=Close\n\n// next\n") == 0,
        "only the value changed");
    check(text->dirty, "the document is dirty");
    check(!Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD), "the same edit again is stale");

    field_cmd(&command, "loc:door_a", "desc", "", "A door.");
    check(apply_push(&undo, &command), "CFG_FIELD add applies");
    field_cmd(&command, "loc:door_a", "desc", "", "Again");
    check(!Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD),
        "a plain add of a key that exists is refused");

    /* Delete the whole record in one stroke: fields with their lines, then
     * the header. One undo brings it back byte for byte. */
    Editor_UndoStrokeBegin(&undo);
    {
        static const char* const keys[] = { "name", "op1", "op2", "desc" };
        struct Editor_CfgBlock block;

        for( size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++ )
        {
            char spec[EDITOR_CMD_NAME_MAX];
            char value[EDITOR_CMD_VALUE_MAX];
            size_t value_length;
            const char* current;
            int line;

            Editor_CfgBlockInit(&block);
            Editor_CfgBlockSet(&block, text->text, text->length);
            line = Editor_CfgBlockFieldFind(&block, keys[k], 0);
            current = Editor_CfgBlockValue(&block, line, &value_length);
            snprintf(value, sizeof(value), "%.*s", (int)value_length, current);
            Editor_CfgFieldSpecFormat(keys[k], 0, line, spec, sizeof(spec));
            Editor_CfgBlockFree(&block);

            field_cmd(&command, "loc:door_a", spec, value, "");
            check(apply_push(&undo, &command), "a field removal with its line applies");
        }
        Editor_CmdInit(&command, EDITOR_DOC_CFG, "loc:door_a", EDITOR_CMD_CFG_BLOCK);
        snprintf(command.u.cfg_block.name, sizeof(command.u.cfg_block.name), "door_a");
        command.has_before = 1;
        check(apply_push(&undo, &command), "CFG_BLOCK remove of the emptied record applies");
    }
    Editor_UndoStrokeEnd(&undo);
    check(strcmp(text->text, "\n// next\n") == 0, "the record is gone, the next one's comment stays");

    check(Editor_UndoUndo(&undo, &g_doc, NULL, NULL) == 5, "one undo reverts the stroke");
    check(strcmp(text->text, "[door_a]\nname=Gate\nop1=Open\nop2=Close\ndesc=A door.\n\n// next\n")
              == 0,
        "the record is back, every field in its place");
    check(Editor_UndoUndo(&undo, &g_doc, NULL, NULL) == 1, "undo the add");
    check(Editor_UndoUndo(&undo, &g_doc, NULL, NULL) == 1, "undo the set");
    check(strcmp(text->text, original) == 0, "back to the loaded bytes");
    check(Editor_UndoRedo(&undo, &g_doc, NULL, NULL) == 1, "redo the set");
    check(strstr(text->text, "name=Gate") != NULL, "redo re-applies");

    /* Rename, and its inverse. */
    Editor_CmdInit(&command, EDITOR_DOC_CFG, "loc:door_a", EDITOR_CMD_CFG_BLOCK);
    snprintf(command.u.cfg_block.name, sizeof(command.u.cfg_block.name), "door_a");
    snprintf(command.u.cfg_block.new_name, sizeof(command.u.cfg_block.new_name), "door_b");
    command.has_before = 1;
    command.has_after = 1;
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD), "rename applies");
    check(strncmp(text->text, "[door_b]\n", 9) == 0, "the header is renamed");
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_INVERSE), "rename inverts");
    check(strncmp(text->text, "[door_a]\n", 9) == 0, "the old name is back");

    /* A new record: add into an empty document, then fields. */
    text = open_cfg("loc:new_door", "");
    Editor_CmdInit(&command, EDITOR_DOC_CFG, "loc:new_door", EDITOR_CMD_CFG_BLOCK);
    snprintf(command.u.cfg_block.name, sizeof(command.u.cfg_block.name), "new_door");
    command.has_after = 1;
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD), "add a record");
    field_cmd(&command, "loc:new_door", "name", "", "New door");
    check(Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD), "and a field");
    check(strcmp(text->text, "[new_door]\nname=New door\n") == 0, "the new record's text");

    /* Off-the-wire garbage is refused, not trusted. */
    field_cmd(&command, "loc:new_door", "name", "New door", "x");
    memset(command.u.cfg_field.key, 'k', sizeof(command.u.cfg_field.key));
    check(!Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD), "an unterminated key is refused");
    field_cmd(&command, "loc:new_door", "na=me", "", "x");
    check(!Editor_CmdApply(&g_doc, &command, EDITOR_CMD_FORWARD), "a key with '=' is refused");

    Editor_UndoFree(&undo);
    Editor_DocFree(&g_doc);
}

int
main(
    int argc,
    char** argv)
{
    test_specs();
    test_lines();
    test_edits();
    test_file();
    test_commands();
    if( argc > 1 )
        test_tree(argv[1]);
    free(g_emit);
    printf("editor_cfg_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
