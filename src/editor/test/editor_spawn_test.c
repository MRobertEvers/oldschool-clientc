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


/* SPAWN_EDIT: a row rewritten where it stands; every other line untouched,
 * the inverse byte-exact, and the edit plans for each kind of listed row. */
static void
test_edit_in_place(void)
{
    const char* source = "// header\n"
                         "==== NPC ====\n"
                         "giantspider1    3200  3238 0   // hand-spaced\n"
                         "rat                                           3205  3203 0\n"
                         "\n"
                         "==== OBJ ====\n"
                         "knife                                         3205  3212 0 3\n";
    struct Editor_SpawnFile file;
    struct Editor_SpawnRow spider;
    struct Editor_SpawnRow moved;
    struct Editor_Cmd command;
    struct Editor_Cmd plan[4];
    char* text;
    int n;

    Editor_SpawnInit(&file);
    check(Editor_SpawnParse(&file, source, strlen(source)).ok, "edit: the fixture parses");
    spider = file.lines[2].row;
    moved = spider;
    moved.abs_x = 3201;
    moved.level = 1;

    Editor_SpawnEditCommand(&command, "m50_50", &spider, &moved, &file);
    check(command.kind == EDITOR_CMD_SPAWN_EDIT && strcmp(command.u.spawn_edit.before_text, file.lines[2].text) == 0,
        "edit: the command carries the old line's bytes");
    check(Editor_SpawnApplyEdit(&file, &command, 0) == 1, "edit: applies");
    text = emit(&file);
    check(strcmp(text,
              "// header\n"
              "==== NPC ====\n"
              "giantspider1                                  3201  3238 1   // hand-spaced\n"
              "rat                                           3205  3203 0\n"
              "\n"
              "==== OBJ ====\n"
              "knife                                         3205  3212 0 3\n")
              == 0,
        "edit: only that line changed, formatted like the generator's, its comment kept");
    free(text);
    check(Editor_SpawnApplyEdit(&file, &command, 0) == 0, "edit: a second forward apply finds no old row");
    check(Editor_SpawnApplyEdit(&file, &command, 1) == 1, "edit: the inverse applies");
    text = emit(&file);
    check(strcmp(text, source) == 0, "edit: and restores the hand-spaced line byte for byte");
    free(text);

    /* Without before_text (a controller): the row still comes back. */
    command.u.spawn_edit.before_text[0] = '\0';
    check(Editor_SpawnApplyEdit(&file, &command, 0) == 1 && Editor_SpawnApplyEdit(&file, &command, 1) == 1
              && Editor_SpawnFind(&file, &spider) == 2,
        "edit: with no before_text the inverse reformats the row on its line");

    /* A suppression mark is a field like the others. */
    {
        struct Editor_SpawnRow rat = file.lines[3].row;
        struct Editor_SpawnRow mark = rat;
        mark.suppress = 1;
        Editor_SpawnEditCommand(&command, "m50_50", &rat, &mark, &file);
        check(Editor_SpawnApplyEdit(&file, &command, 0) == 1 && file.lines[3].text[0] == '-'
                  && file.lines[3].row.suppress,
            "edit: Suppressed rewrites the row as -rat on its line");
        check(Editor_SpawnApplyEdit(&file, &command, 1) == 1 && strcmp(file.lines[3].text, "rat                                           3205  3203 0") == 0,
            "edit: and back");
    }
    /* A kind change does not fit one section. */
    {
        struct Editor_SpawnRow knife = file.lines[6].row;
        struct Editor_SpawnRow as_npc = knife;
        as_npc.is_obj = 0;
        Editor_SpawnEditCommand(&command, "m50_50", &knife, &as_npc, &file);
        check(Editor_SpawnApplyEdit(&file, &command, 0) == 0, "edit: SPAWN_EDIT refuses npc <-> obj");
    }

    /* An add that opened a section: its delete takes the banner back too. */
    {
        const char* npc_only = "==== NPC ====\nrat 3205 3203 0\n";
        struct Editor_SpawnFile small;
        struct Editor_SpawnRow knife;
        char* out;
        int at;
        Editor_SpawnInit(&small);
        (void)Editor_SpawnParse(&small, npc_only, strlen(npc_only));
        memset(&knife, 0, sizeof(knife));
        knife.is_obj = 1;
        snprintf(knife.name, sizeof(knife.name), "knife");
        knife.abs_x = 3205;
        knife.abs_z = 3203;
        knife.count = 1;
        at = Editor_SpawnInsert(&small, &knife);
        Editor_SpawnRemoveRow(&small, at);
        out = emit(&small);
        check(strcmp(out, npc_only) == 0, "remove: an add's delete takes back the OBJ banner it opened");
        free(out);
        Editor_SpawnFree(&small);
    }

    /* The plans. */
    {
        struct Editor_SpawnEditSource source_row;
        struct Editor_SpawnRow want;

        memset(&source_row, 0, sizeof(source_row));
        source_row.row = spider;
        source_row.edited = 1;
        want = moved;
        n = Editor_SpawnEditPlan(&file, &source_row, &want, plan, 4);
        check(n == 1 && plan[0].kind == EDITOR_CMD_SPAWN_EDIT && strcmp(plan[0].doc_key, "m50_50") == 0,
            "plan: an edited row in its own square is one SPAWN_EDIT");
        want.abs_x = 3200 + 64;
        n = Editor_SpawnEditPlan(&file, &source_row, &want, plan, 4);
        check(n == 2 && plan[0].kind == EDITOR_CMD_SPAWN && plan[0].has_before && strcmp(plan[0].doc_key, "m50_50") == 0
                  && plan[1].kind == EDITOR_CMD_SPAWN && plan[1].has_after && strcmp(plan[1].doc_key, "m51_50") == 0,
            "plan: across a square border it is a delete and an add");
        want = spider;
        want.is_obj = 1;
        want.count = 2;
        n = Editor_SpawnEditPlan(&file, &source_row, &want, plan, 4);
        check(n == 2, "plan: npc -> obj is a delete and an add");
        want = spider;
        check(Editor_SpawnEditPlan(&file, &source_row, &want, plan, 4) == 0, "plan: no change, no command");
        want.level = 4;
        check(Editor_SpawnEditPlan(&file, &source_row, &want, plan, 4) == -1, "plan: level 4 is refused");
        want = spider;
        snprintf(want.name, sizeof(want.name), "two words");
        check(Editor_SpawnEditPlan(&file, &source_row, &want, plan, 4) == -1, "plan: a spaced name is refused");
        snprintf(want.name, sizeof(want.name), "#12x");
        check(Editor_SpawnEditPlan(&file, &source_row, &want, plan, 4) == -1, "plan: #12x is not an id");
        snprintf(want.name, sizeof(want.name), "#12");
        check(Editor_SpawnEditPlan(&file, &source_row, &want, plan, 4) == 1, "plan: #12 is the id escape");

        /* A generated row. */
        source_row.edited = 0;
        source_row.suppressed = 0;
        want = spider;
        want.suppress = 1;
        n = Editor_SpawnEditPlan(NULL, &source_row, &want, plan, 4);
        check(n == 1 && plan[0].kind == EDITOR_CMD_SPAWN_SUPPRESS && plan[0].has_after,
            "plan: Suppressed on a generated row adds its suppression row");
        source_row.suppressed = 1;
        want.suppress = 0;
        n = Editor_SpawnEditPlan(NULL, &source_row, &want, plan, 4);
        check(n == 1 && plan[0].kind == EDITOR_CMD_SPAWN_SUPPRESS && plan[0].has_before,
            "plan: clearing it restores the generated spawn");
        source_row.suppressed = 0;
        want = moved;
        n = Editor_SpawnEditPlan(NULL, &source_row, &want, plan, 4);
        check(n == 2 && plan[0].kind == EDITOR_CMD_SPAWN_SUPPRESS && plan[0].has_after
                  && plan[1].kind == EDITOR_CMD_SPAWN && plan[1].has_after && plan[1].u.spawn.abs_x == 3201,
            "plan: a changed generated row is suppressed and the edited row added");
        source_row.suppressed = 1;
        n = Editor_SpawnEditPlan(NULL, &source_row, &want, plan, 4);
        check(n == 1 && plan[0].kind == EDITOR_CMD_SPAWN, "plan: already suppressed, only the add");
    }
    Editor_SpawnFree(&file);
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
    test_edit_in_place();

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
