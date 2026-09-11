/**
 * The editor renders what the game renders.
 *
 * The editor loads a square from the `.jm2` text in the content tree, not from
 * the baked cache, and seeds the provider with what that text derives to. This
 * test checks the claim that makes the whole arrangement safe: for a square
 * the user has not edited, deriving from text produces exactly the terrain the
 * cache would have produced — same resolved heights, same overlays, same
 * everything the world builder meshes from.
 *
 * It is the one test that exercises the height fixup end to end, and so the
 * one that would catch a drift between the authored form and the renderer's:
 * a wrong tile-width flag, a mis-set `height_authored`, a shape/rotation that
 * does not fold back into its attribute opcode.
 *
 *   editor_derive_test <content-dir> <cache-dir> [max-squares]
 *
 * Read-only on both.
 */

#include "editor/editor_derive.h"
#include "editor/editor_doc.h"
#include "editor/editor_jm2.h"
#include "engine/torirs_types.h"

#include <rscache.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_squares;
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
    rewind(f);
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

static void
check_square(
    const char* content_dir,
    struct RSCache_Dat2Disk* cache,
    const struct RSCache* profile,
    int map_x,
    int map_z)
{
    char path[1200];
    char* text;
    size_t text_size = 0;
    struct Editor_Square square;
    struct Editor_ParseResult parsed;
    struct RSCache_MapTerrain* reference;
    struct ToriRS_MapTerrain* derived;
    int mismatches = 0;

    snprintf(path, sizeof(path), "%s/maps/m%d_%d.jm2", content_dir, map_x, map_z);
    text = slurp(path, &text_size);
    if( !text )
        return;

    /*
     * Resolve the square the way the client does, not via the named-archive
     * helper: at this revision the maps table is region-grouped and carries no
     * `mX_Z` names at all, so a name lookup finds nothing for every square.
     * MapTerrainNewFromArchiveProfile splits the region archive internally.
     */
    {
        struct RSCache_Dat2DiskArchive* archive = NULL;
        int region_id = RSCache_MapSquareId(map_x, map_z);

        if( region_id >= 0 )
            archive = RSCache_Dat2DiskArchiveNewLoad(cache, RSCACHE_DAT2_TABLE_MAPS, region_id);
        if( !archive )
        {
            /* The content tree can carry squares the cache does not ship. */
            free(text);
            return;
        }
        reference = RSCache_MapTerrainNewFromArchiveProfile(archive, map_x, map_z, profile);
        RSCache_Dat2DiskArchiveFree(archive);
    }
    if( !reference )
    {
        free(text);
        return;
    }

    Editor_SquareInit(&square, map_x, map_z);
    parsed = Editor_Jm2Parse(&square, text, text_size);
    free(text);
    if( parsed.status != EDITOR_PARSE_OK )
    {
        fprintf(stderr, "FAIL m%d_%d: parse error %d\n", map_x, map_z, (int)parsed.status);
        g_failed++;
        RSCache_MapTerrainFree(reference);
        Editor_SquareFree(&square);
        return;
    }

    derived = malloc(sizeof(*derived));
    if( !derived )
    {
        RSCache_MapTerrainFree(reference);
        Editor_SquareFree(&square);
        return;
    }
    memset(derived, 0, sizeof(*derived));

    if( !Editor_SquareDeriveTerrain(&square, profile, derived) )
    {
        fprintf(stderr, "FAIL m%d_%d: derive failed\n", map_x, map_z);
        g_failed++;
        free(derived);
        RSCache_MapTerrainFree(reference);
        Editor_SquareFree(&square);
        return;
    }

    g_squares++;
    for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
    {
        const struct RSCache_MapFloor* want = &reference->tiles_xyz[i];
        const struct ToriRS_MapFloor* got = &derived->tiles_xyz[i];

        /*
         * Authored as well as resolved.
         *
         * The cache decode records height provenance whether or not the fixup
         * ran, so this checks the other half of the claim: the editor's text
         * parse agrees with the cache about which tiles the FILE gave a height,
         * not just about where the ground ends up. Getting `height` right while
         * getting this wrong is exactly the failure that bakes generated
         * terrain into a saved square.
         */
        if( want->height_authored != got->has_authored_height ||
            (want->height_authored && want->authored_height != got->authored_height) )
        {
            if( mismatches == 0 )
                fprintf(
                    stderr,
                    "FAIL m%d_%d tile %d: authored %d/%d value %d/%d (cache/derived)\n",
                    map_x, map_z, i, want->height_authored, got->has_authored_height,
                    want->authored_height, got->authored_height);
            mismatches++;
        }
        else if(
            want->height != got->height || want->overlay_id != got->overlay_id ||
            want->underlay_id != got->underlay_id || want->settings != got->settings ||
            want->shape != got->shape || want->rotation != got->rotation )
        {
            if( mismatches == 0 )
            {
                fprintf(
                    stderr,
                    "FAIL m%d_%d tile %d: height %d/%d overlay %d/%d underlay %d/%d "
                    "settings %d/%d shape %d/%d rot %d/%d (cache/derived)\n",
                    map_x, map_z, i, want->height, got->height, want->overlay_id,
                    got->overlay_id, want->underlay_id, got->underlay_id, want->settings,
                    got->settings, want->shape, got->shape, want->rotation, got->rotation);
            }
            mismatches++;
        }
    }
    if( mismatches > 0 )
    {
        fprintf(stderr, "FAIL m%d_%d: %d tiles differ\n", map_x, map_z, mismatches);
        g_failed++;
    }

    free(derived);
    RSCache_MapTerrainFree(reference);
    Editor_SquareFree(&square);
}

int
main(
    int argc,
    char** argv)
{
    const char* content_dir;
    const char* cache_dir;
    int limit;
    struct RSCache_Dat2Disk* cache;
    struct RSCache profile;
    char maps_dir[1024];
    DIR* dir;
    struct dirent* entry;
    int seen = 0;

    if( argc < 3 )
    {
        fprintf(stderr, "usage: %s <content-dir> <cache-dir> [max-squares]\n", argv[0]);
        return 2;
    }
    content_dir = argv[1];
    cache_dir = argv[2];
    limit = argc > 3 ? atoi(argv[3]) : 24;

    if( !RSCache_ProfileByName("osrs239", &profile) )
    {
        fprintf(stderr, "unknown profile osrs239\n");
        return 2;
    }

    cache = RSCache_Dat2DiskNewReadOnlyFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "cannot open cache %s\n", cache_dir);
        return 2;
    }

    snprintf(maps_dir, sizeof(maps_dir), "%s/maps", content_dir);
    dir = opendir(maps_dir);
    if( !dir )
    {
        fprintf(stderr, "cannot open %s\n", maps_dir);
        return 2;
    }

    while( (entry = readdir(dir)) != NULL && seen < limit )
    {
        int map_x;
        int map_z;
        size_t name_length = strlen(entry->d_name);

        if( name_length < 5 || strcmp(entry->d_name + name_length - 4, ".jm2") != 0 )
            continue;
        if( sscanf(entry->d_name, "m%d_%d.jm2", &map_x, &map_z) != 2 )
            continue;

        seen++;
        check_square(content_dir, cache, &profile, map_x, map_z);
    }
    closedir(dir);

    printf(
        "editor_derive_test: %d squares compared against the cache, %d failed\n",
        g_squares,
        g_failed);
    return g_failed == 0 ? 0 : 1;
}
