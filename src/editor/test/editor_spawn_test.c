/**
 * The `.spawn` codec.
 *
 *   editor_spawn_test <generated-configs-dir> [<scripts-root>]
 *
 * The round trip is the whole contract: every generated file under
 * `server/scripts/areas/world/configs/` parses and emits back byte for byte,
 * because a save rewrites the file and anything else would put edits the
 * user never made into the diff. With a scripts root, every other `.spawn`
 * under it (the hand-authored areas) is held to the same rule.
 *
 * Then the edit operations: rows land in the right section in the
 * generator's format, a suppression row reads back as one, a new section is
 * opened when the file has none, and delete finds exactly the row it names.
 *
 * Read-only over the content tree.
 */

#include "editor/editor_spawn.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_size = (size_t)size;
    return data;
}

/** Returns 1 when the file round-trips byte for byte. */
static int
round_trip_file(
    const char* path,
    int* out_rows)
{
    size_t size = 0;
    char* text = slurp(path, &size);
    struct Editor_SpawnFile file;
    struct Editor_SpawnParseResult result;
    size_t emitted_size;
    char* emitted;
    int ok;

    if( !text )
    {
        fprintf(stderr, "cannot read %s\n", path);
        return 0;
    }
    Editor_SpawnInit(&file);
    result = Editor_SpawnParse(&file, text, size);
    if( !result.ok )
    {
        fprintf(stderr, "%s:%d: does not parse\n", path, result.line);
        Editor_SpawnFree(&file);
        free(text);
        return 0;
    }
    emitted_size = Editor_SpawnEmit(&file, NULL, 0);
    emitted = malloc(emitted_size + 1);
    if( !emitted )
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    Editor_SpawnEmit(&file, emitted, emitted_size + 1);
    ok = emitted_size == size && memcmp(emitted, text, size) == 0;
    if( !ok )
        fprintf(stderr, "%s: emit differs (%zu bytes vs %zu)\n", path, emitted_size, size);
    *out_rows += Editor_SpawnRowCount(&file);
    free(emitted);
    Editor_SpawnFree(&file);
    free(text);
    return ok;
}

static int
ends_with(
    const char* name,
    const char* suffix)
{
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);
    return name_length > suffix_length
           && strcmp(name + name_length - suffix_length, suffix) == 0;
}

/** Every `.spawn` directly in `dir`. */
static void
round_trip_dir(
    const char* dir,
    int* out_files,
    int* out_failed,
    int* out_rows)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;

    if( !handle )
    {
        fprintf(stderr, "cannot open %s\n", dir);
        (*out_failed)++;
        return;
    }
    while( (entry = readdir(handle)) != NULL )
    {
        char path[2048];
        if( !ends_with(entry->d_name, ".spawn") )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        (*out_files)++;
        if( !round_trip_file(path, out_rows) )
            (*out_failed)++;
    }
    closedir(handle);
}

/** Every `.spawn` under `root`, recursively, except under `skip`. */
static void
round_trip_tree(
    const char* root,
    const char* skip,
    int* out_files,
    int* out_failed,
    int* out_rows)
{
    DIR* handle = opendir(root);
    struct dirent* entry;

    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        char path[2048];
        struct stat info;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
        if( strcmp(path, skip) == 0 || stat(path, &info) != 0 )
            continue;
        if( S_ISDIR(info.st_mode) )
            round_trip_tree(path, skip, out_files, out_failed, out_rows);
        else if( ends_with(entry->d_name, ".spawn") )
        {
            (*out_files)++;
            if( !round_trip_file(path, out_rows) )
                (*out_failed)++;
        }
    }
    closedir(handle);
}

static char*
emit(const struct Editor_SpawnFile* file)
{
    size_t size = Editor_SpawnEmit(file, NULL, 0);
    char* text = malloc(size + 1);
    if( !text )
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    Editor_SpawnEmit(file, text, size + 1);
    return text;
}

static void
test_edits(void)
{
    const char* source = "// comment kept\n"
                         "==== NPC ====\n"
                         "giantspider1                                  3200  3238 0\n"
                         "-goblin_unarmed_melee_4  3202  3253 0      // suppress it\n"
                         "\n";
    struct Editor_SpawnFile file;
    struct Editor_SpawnRow row;
    struct Editor_SpawnParseResult result;
    char* text;
    int index;

    Editor_SpawnInit(&file);
    result = Editor_SpawnParse(&file, source, strlen(source));
    check(result.ok, "a file with a suppression row parses");
    check(Editor_SpawnRowCount(&file) == 2, "two rows, one of them a suppression");
    check(
        file.lines[3].kind == EDITOR_SPAWN_LINE_ROW && file.lines[3].row.suppress
            && strcmp(file.lines[3].row.name, "goblin_unarmed_melee_4") == 0
            && file.lines[3].row.abs_x == 3202 && file.lines[3].row.abs_z == 3253,
        "the suppression row reads back without its '-' and with its tile");

    memset(&row, 0, sizeof(row));
    row.suppress = 1;
    snprintf(row.name, sizeof(row.name), "goblin_unarmed_melee_4");
    row.abs_x = 3202;
    row.abs_z = 3253;
    row.count = 1;
    check(Editor_SpawnFind(&file, &row) == 3, "find names the suppression row");
    row.suppress = 0;
    check(Editor_SpawnFind(&file, &row) == -1, "a spawn row is not its suppression");

    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "rat");
    row.abs_x = 3205;
    row.abs_z = 3203;
    row.count = 1;
    index = Editor_SpawnInsert(&file, &row);
    check(index == 4, "a new npc row goes after the section's last row, before the blank");

    memset(&row, 0, sizeof(row));
    row.is_obj = 1;
    snprintf(row.name, sizeof(row.name), "knife");
    row.abs_x = 3205;
    row.abs_z = 3212;
    row.count = 3;
    Editor_SpawnInsert(&file, &row);
    text = emit(&file);
    check(
        strcmp(
            text,
            "// comment kept\n"
            "==== NPC ====\n"
            "giantspider1                                  3200  3238 0\n"
            "-goblin_unarmed_melee_4  3202  3253 0      // suppress it\n"
            "rat                                           3205  3203 0\n"
            "\n"
            "==== OBJ ====\n"
            "knife                                         3205  3212 0 3\n")
            == 0,
        "rows are formatted like the generator's and an OBJ section is opened");
    free(text);

    row.count = 2;
    check(Editor_SpawnFind(&file, &row) == -1, "an obj row is matched with its count");
    row.count = 3;
    index = Editor_SpawnFind(&file, &row);
    check(index == 7, "and found with it");
    Editor_SpawnRemoveAt(&file, index);
    check(Editor_SpawnRowCount(&file) == 3, "delete removes exactly that row");
    Editor_SpawnFree(&file);

    /* Malformed text is data, reported with its line. */
    {
        const char* bad = "==== NPC ====\nrat 1 2\n";
        result = Editor_SpawnParse(&file, bad, strlen(bad));
        check(!result.ok && result.line == 2, "a short row reports its line");
        Editor_SpawnFree(&file);
        bad = "rat 3200 3200 0\n";
        result = Editor_SpawnParse(&file, bad, strlen(bad));
        check(!result.ok && result.line == 1, "a row before any banner is an error");
        Editor_SpawnFree(&file);
        bad = "==== NPC ====\nrat 3200 3200 0 4\n";
        result = Editor_SpawnParse(&file, bad, strlen(bad));
        check(!result.ok, "an npc row has no count");
        Editor_SpawnFree(&file);
    }

    /* Empty in, empty out; no final newline survives as none. */
    {
        result = Editor_SpawnParse(&file, NULL, 0);
        text = emit(&file);
        check(result.ok && text[0] == '\0', "an empty file round-trips");
        free(text);
        result = Editor_SpawnParse(&file, "// x", 4);
        text = emit(&file);
        check(result.ok && strcmp(text, "// x") == 0, "a missing final newline is kept missing");
        free(text);
        Editor_SpawnFree(&file);
    }
}

int
main(
    int argc,
    char** argv)
{
    int files = 0;
    int failed = 0;
    int rows = 0;

    if( argc < 2 || argc > 3 )
    {
        fprintf(stderr, "usage: editor_spawn_test <generated-configs-dir> [<scripts-root>]\n");
        return 2;
    }

    test_edits();

    round_trip_dir(argv[1], &files, &failed, &rows);
    check(files > 0, "the generated directory holds spawn files");
    check(failed == 0, "every generated spawn file round-trips byte for byte");
    printf("editor_spawn_test: generated: %d files, %d rows, %d not byte-exact\n",
        files, rows, failed);

    if( argc == 3 )
    {
        int other_files = 0;
        int other_failed = 0;
        int other_rows = 0;
        round_trip_tree(argv[2], argv[1], &other_files, &other_failed, &other_rows);
        check(other_failed == 0, "every hand-authored spawn file round-trips byte for byte");
        printf("editor_spawn_test: hand-authored: %d files, %d rows, %d not byte-exact\n",
            other_files, other_rows, other_failed);
    }

    printf("editor_spawn_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
