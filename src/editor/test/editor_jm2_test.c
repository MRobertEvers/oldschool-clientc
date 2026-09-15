/**
 * Codec fidelity: every `.jm2` and `.jl2` in a content tree must survive
 * parse -> emit unchanged, byte for byte.
 *
 * This is the gate the whole editor rests on. The editor's save path rewrites
 * the file it loaded, so any asymmetry in the codec is a diff the user did not
 * ask for — a dropped `trailing=` line, a re-ordered token, a lost server
 * spawn section — landing in git across thousands of squares at once. Testing
 * it against synthetic input would only prove the codec agrees with itself;
 * this runs it over the real tree, which is where the odd squares live (the
 * one with 9,564 trailing bytes, the ones with foreign sections, the ones with
 * no locs at all).
 *
 *   editor_jm2_test <content-dir> [max-squares]
 *
 * Read-only: it never writes to the content tree.
 */

#include "editor/editor_doc.h"
#include "editor/editor_jm2.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checked;
static int g_failed;

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
    fseek(f, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(f);
        return NULL;
    }

    data = malloc((size_t)size + 1);
    if( !data )
    {
        fclose(f);
        return NULL;
    }
    if( fread(data, 1, (size_t)size, f) != (size_t)size )
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

/** Report the first byte that differs, with a window of context each side. */
static void
report_mismatch(
    const char* path,
    const char* want,
    size_t want_size,
    const char* got,
    size_t got_size)
{
    size_t at = 0;
    while( at < want_size && at < got_size && want[at] == got[at] )
        at++;

    fprintf(stderr, "FAIL %s\n", path);
    fprintf(stderr, "  sizes: original %zu, emitted %zu\n", want_size, got_size);
    fprintf(stderr, "  first difference at byte %zu\n", at);

    {
        size_t from = at > 40 ? at - 40 : 0;
        size_t want_to = at + 40 < want_size ? at + 40 : want_size;
        size_t got_to = at + 40 < got_size ? at + 40 : got_size;
        fprintf(stderr, "  original: %.*s\n", (int)(want_to - from), want + from);
        fprintf(stderr, "  emitted : %.*s\n", (int)(got_to - from), got + from);
    }
}

/**
 * @param parse  parser for this half of the square
 * @param emit   matching emitter
 */
static void
check_file(
    const char* path,
    int map_x,
    int map_z,
    struct Editor_ParseResult (*parse)(struct Editor_Square*, const char*, size_t),
    size_t (*emit)(const struct Editor_Square*, char*, size_t))
{
    struct Editor_Square square;
    struct Editor_ParseResult result;
    char* original;
    size_t original_size = 0;
    char* emitted;
    size_t emitted_size;

    original = slurp(path, &original_size);
    if( !original )
        return; /* Half the pair may legitimately not exist. */

    Editor_SquareInit(&square, map_x, map_z);

    result = parse(&square, original, original_size);
    if( result.status != EDITOR_PARSE_OK )
    {
        fprintf(
            stderr, "FAIL %s: parse status %d at line %d\n", path, (int)result.status, result.line);
        g_failed++;
        Editor_SquareFree(&square);
        free(original);
        return;
    }

    /* Probe for the length, then emit for real — the contract a caller sizing
     * a save buffer uses, so the test exercises it too. */
    emitted_size = emit(&square, NULL, 0);
    emitted = malloc(emitted_size + 1);
    if( !emitted )
    {
        fprintf(stderr, "FAIL %s: out of memory for %zu bytes\n", path, emitted_size);
        g_failed++;
        Editor_SquareFree(&square);
        free(original);
        return;
    }
    emit(&square, emitted, emitted_size + 1);

    g_checked++;
    if( emitted_size != original_size || memcmp(original, emitted, original_size) != 0 )
    {
        report_mismatch(path, original, original_size, emitted, emitted_size);
        g_failed++;
    }

    free(emitted);
    Editor_SquareFree(&square);
    free(original);
}

int
main(
    int argc,
    char** argv)
{
    const char* content_dir;
    char maps_dir[1024];
    DIR* dir;
    struct dirent* entry;
    int limit;
    int squares = 0;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <content-dir> [max-squares]\n", argv[0]);
        return 2;
    }
    content_dir = argv[1];
    limit = argc > 2 ? atoi(argv[2]) : 0;

    snprintf(maps_dir, sizeof(maps_dir), "%s/maps", content_dir);
    dir = opendir(maps_dir);
    if( !dir )
    {
        fprintf(stderr, "cannot open %s\n", maps_dir);
        return 2;
    }

    while( (entry = readdir(dir)) != NULL )
    {
        int map_x;
        int map_z;
        char path[1200];
        const char* name = entry->d_name;
        size_t name_length = strlen(name);

        /* Drive off the .jm2 and check its .jl2 beside it, so a square is
         * always checked as a pair. */
        if( name_length < 5 || strcmp(name + name_length - 4, ".jm2") != 0 )
            continue;
        if( sscanf(name, "m%d_%d.jm2", &map_x, &map_z) != 2 )
            continue;
        if( limit > 0 && squares >= limit )
            break;
        squares++;

        snprintf(path, sizeof(path), "%s/m%d_%d.jm2", maps_dir, map_x, map_z);
        check_file(path, map_x, map_z, Editor_Jm2Parse, Editor_Jm2Emit);

        snprintf(path, sizeof(path), "%s/m%d_%d.jl2", maps_dir, map_x, map_z);
        check_file(path, map_x, map_z, Editor_Jl2Parse, Editor_Jl2Emit);
    }
    closedir(dir);

    printf(
        "editor_jm2_test: %d squares, %d files checked, %d failed\n", squares, g_checked, g_failed);
    return g_failed == 0 ? 0 : 1;
}
