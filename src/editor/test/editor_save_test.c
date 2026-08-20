/**
 * The save path, end to end through the real host: load a square from a content
 * tree, edit it, save, and read back what landed on disk.
 *
 * This is the half the codec tests cannot reach. They prove a square survives
 * parse -> emit; this proves the editor writes the file it meant to, writes
 * ONLY the half it changed, leaves an untouched square byte-identical, and
 * refuses to write at all without the session lock.
 *
 *   editor_save_test <scratch-dir> <source-jm2> <source-jl2>
 *
 * Writes only inside the scratch directory it is given.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_host.h"
#include "editor/editor_jm2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    char const* what)
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
    char const* path,
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
    if( !data || fread(data, 1, (size_t)size, f) != (size_t)size )
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

static void
copy_file(
    char const* from,
    char const* to)
{
    size_t size = 0;
    char* data = slurp(from, &size);
    FILE* f;
    if( !data )
    {
        fprintf(stderr, "cannot read %s\n", from);
        exit(2);
    }
    f = fopen(to, "wb");
    if( !f )
    {
        fprintf(stderr, "cannot write %s\n", to);
        exit(2);
    }
    fwrite(data, 1, size, f);
    fclose(f);
    free(data);
}

int
main(
    int argc,
    char** argv)
{
    char maps_dir[1024];
    char jm2_path[1200];
    char jl2_path[1200];
    struct EditorHost host;
    struct Editor_Doc* doc;
    struct Editor_Square* square;
    struct EditorHost_Blob jm2 = { NULL, 0 };
    struct EditorHost_Blob jl2 = { NULL, 0 };
    size_t original_jl2_size = 0;
    char* original_jl2;

    if( argc < 4 )
    {
        fprintf(stderr, "usage: %s <scratch-dir> <source-jm2> <source-jl2>\n", argv[0]);
        return 2;
    }

    snprintf(maps_dir, sizeof(maps_dir), "%s/maps", argv[1]);
    snprintf(jm2_path, sizeof(jm2_path), "%s/m50_50.jm2", maps_dir);
    snprintf(jl2_path, sizeof(jl2_path), "%s/m50_50.jl2", maps_dir);
    copy_file(argv[2], jm2_path);
    copy_file(argv[3], jl2_path);
    original_jl2 = slurp(jl2_path, &original_jl2_size);

    Editor_HostOpenLocal(&host, argv[1], NULL);
    doc = calloc(1, sizeof(*doc));

    /* Saving must be refused before the lock is taken -- otherwise two sessions
     * could interleave writes into the same square. */
    check(
        host.vtable->square_save(host.user_data, 50, 50, "x", 1, NULL, 0) == EDITOR_HOST_LOCKED,
        "save is refused without the session lock");

    check(
        host.vtable->session(host.user_data, 1) == EDITOR_HOST_OK,
        "the session lock can be taken");

    check(
        host.vtable->square_load(host.user_data, 50, 50, &jm2, &jl2) == EDITOR_HOST_OK,
        "the square loads");

    square = Editor_DocOpenSquare(doc, 50, 50);
    check(Editor_Jm2Parse(square, jm2.data, jm2.size).status == EDITOR_PARSE_OK, "jm2 parses");
    check(Editor_Jl2Parse(square, jl2.data, jl2.size).status == EDITOR_PARSE_OK, "jl2 parses");
    Editor_HostBlobFree(&jm2);
    Editor_HostBlobFree(&jl2);

    /* Edit one tile's underlay, and nothing else. */
    {
        struct Editor_Cmd command;
        int const index = Editor_TileIndex(20, 20, 0);

        memset(&command, 0, sizeof(command));
        command.kind = EDITOR_CMD_TILE;
        command.map_x = 50;
        command.map_z = 50;
        command.x = 20;
        command.z = 20;
        command.tile_before = square->tiles[index];
        command.tile_after = command.tile_before;
        command.tile_after.underlay_id = 77;
        Editor_CmdApply(doc, &command, EDITOR_CMD_FORWARD);
    }

    check(square->dirty_map == 1, "the terrain half is dirty");
    check(square->dirty_loc == 0, "the loc half is NOT dirty");

    /* Write only the dirty half, exactly as Editor_SaveAll does. */
    {
        size_t length = Editor_Jm2Emit(square, NULL, 0);
        char* text = malloc(length + 1);
        Editor_Jm2Emit(square, text, length + 1);
        check(
            host.vtable->square_save(host.user_data, 50, 50, text, length, NULL, 0) ==
                EDITOR_HOST_OK,
            "the dirty half saves");
        free(text);
    }

    /* The untouched half must be byte-identical: rewriting it would show up as
     * a diff on placements the user never edited. */
    {
        size_t after_size = 0;
        char* after = slurp(jl2_path, &after_size);
        check(
            after && after_size == original_jl2_size &&
                memcmp(after, original_jl2, after_size) == 0,
            "the untouched .jl2 is byte-identical");
        free(after);
    }

    /* Read the saved file back and confirm the edit is in it -- and that it is
     * still parseable, which a half-written file would not be. */
    {
        struct Editor_Doc* reload = calloc(1, sizeof(*reload));
        struct Editor_Square* fresh;
        size_t size = 0;
        char* text = slurp(jm2_path, &size);

        fresh = Editor_DocOpenSquare(reload, 50, 50);
        check(
            text && Editor_Jm2Parse(fresh, text, size).status == EDITOR_PARSE_OK,
            "the saved square parses again");
        check(
            fresh->tiles[Editor_TileIndex(20, 20, 0)].underlay_id == 77,
            "the edit is on disk");
        check(
            fresh->tiles[Editor_TileIndex(21, 20, 0)].underlay_id ==
                square->tiles[Editor_TileIndex(21, 20, 0)].underlay_id,
            "a neighbouring tile is unchanged");
        free(text);
        Editor_DocFree(reload);
        free(reload);
    }

    /* No temporary left behind: the atomic write renames onto the target. */
    {
        char temp_path[1300];
        FILE* f;
        snprintf(temp_path, sizeof(temp_path), "%s.tmp", jm2_path);
        f = fopen(temp_path, "rb");
        check(f == NULL, "no .tmp file is left behind");
        if( f )
            fclose(f);
    }

    host.vtable->session(host.user_data, 0);
    Editor_HostClose(&host);
    Editor_DocFree(doc);
    free(doc);
    free(original_jl2);

    printf("editor_save_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
