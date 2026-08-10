/*
 * map_port -- transcode named RS2 map squares into an OSRS LostCity tree lane.
 *
 * Late RS2 stores terrain and XTEA-encrypted scenery as two named archives
 * (mX_Z and lX_Z), with one-byte terrain opcodes. Modern OldSchool stores the
 * pair as files 0 and 1 of archive (X << 8 | Z), with two-byte terrain opcodes.
 * Copying either container is therefore wrong even before scenery ids diverge.
 *
 * This tool works at the decoded-map layer. It:
 *   - asks the source profile to decode terrain and decrypt locs;
 *   - inventories every scenery/floor dependency;
 *   - rewrites every scenery id through cachepack import's TSV ledger;
 *   - gives referenced floors an isolated destination band; and
 *   - writes editable .jm2/.jl2 plus the map/floor pack metadata to a lane.
 *
 * The lane is deliberately not the content tree's root. A staging build can
 * overlay it without replacing checked-in modern squares (some 2012 route
 * squares also exist in OSRS 239).
 *
 * Usage:
 *   map_port --rev rs727 --cache cache.rs727_preeoc \
 *     --squares 22_99,20_95,16_99,17_99,18_99,20_99,21_99,20_100,21_100,\
 *       20_101,21_101,16_101,17_100,17_101,18_101,40_89,39_89,39_90,39_91,40_90 \
 *     --inventory build/rs2012_map_inventory.tsv
 *
 *   map_port ... --ledger OSRS-Content/osrs239-content/port/rs2012_qbd_td.map \
 *     --out OSRS-Content/osrs239-content/ported/rs2012_qbd_td --apply
 */

#include "asset_access.h"
#include "tool_profile.h"

#include "archive.h"
#include "dat2disk.h"
#include "datatypes/dat2_config_flo.h"
#include "datatypes/maps.h"
#include "filelist.h"
#include "reference_table.h"
#include "rscache_profile.h"
#include "xtea_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MAP_PORT_MKDIR(path) _mkdir(path)
#else
#define MAP_PORT_MKDIR(path) mkdir(path, 0755)
#endif

enum
{
    MAP_PORT_MAX_SQUARES = 256,
    MAP_PORT_PATH = 1600
};

struct IntCount
{
    int id;
    int count;
};

struct IntCounts
{
    struct IntCount* v;
    int n;
    int cap;
};

struct Square
{
    int x;
    int z;
    int terrain_archive;
    int loc_archive;
    int32_t xtea[4];
    struct RSCache_MapTerrain* terrain;
    struct RSCache_MapLocs* locs;
    int nonempty_tiles;
};

struct LocMap
{
    int source;
    int dest;
};

struct Options
{
    const char* rev;
    const char* cache;
    const char* squares;
    const char* inventory;
    const char* ledger;
    const char* material_ledger;
    const char* out;
    const char* base_tree;
    int underlay_base;
    int overlay_base;
    int apply;
};

static int loc_map_find(const struct LocMap* map, int count, int source);

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage: %s --rev NAME --cache DIR --squares X_Z,... --inventory FILE\n"
        "          [--ledger FILE --material-ledger TSV --out LANE --base-tree TREE]\n"
        "          [--underlay-base N --overlay-base N --apply]\n",
        argv0);
}

static int
mkdir_p(const char* path)
{
    char tmp[MAP_PORT_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for( char* p = tmp + 1; *p; p++ )
    {
        if( *p != '/' )
            continue;
        *p = '\0';
        if( MAP_PORT_MKDIR(tmp) != 0 && errno != EEXIST )
            return 0;
        *p = '/';
    }
    return MAP_PORT_MKDIR(tmp) == 0 || errno == EEXIST;
}

static int
ensure_parent(const char* path)
{
    char work[MAP_PORT_PATH];
    snprintf(work, sizeof(work), "%s", path);
    char* slash = strrchr(work, '/');
    if( !slash )
        return 1;
    *slash = '\0';
    return mkdir_p(work);
}

static int
counts_add(struct IntCounts* counts, int id)
{
    if( id < 0 )
        return 1;
    for( int i = 0; i < counts->n; i++ )
    {
        if( counts->v[i].id == id )
        {
            counts->v[i].count++;
            return 1;
        }
    }
    if( counts->n == counts->cap )
    {
        int cap = counts->cap ? counts->cap * 2 : 64;
        void* grown = realloc(counts->v, (size_t)cap * sizeof(*counts->v));
        if( !grown )
            return 0;
        counts->v = grown;
        counts->cap = cap;
    }
    counts->v[counts->n].id = id;
    counts->v[counts->n].count = 1;
    counts->n++;
    return 1;
}

static int
count_compare(const void* a, const void* b)
{
    const struct IntCount* lhs = a;
    const struct IntCount* rhs = b;
    return lhs->id < rhs->id ? -1 : lhs->id > rhs->id ? 1 : 0;
}

static int
counts_index(const struct IntCounts* counts, int id)
{
    /* Floor allocation order is stable across incremental ports, rather than
     * source-id sorted. The lists are tiny, so a linear lookup is both clearer
     * and avoids accidentally depending on source order again. */
    for( int i = 0; i < counts->n; i++ )
        if( counts->v[i].id == id )
            return i;
    return -1;
}

static int
named_map_archive_id(
    struct RSCache_Dat2Disk* disk,
    const char* kind,
    int map_x,
    int map_z)
{
    char name[32];
    snprintf(name, sizeof(name), "%s%d_%d", kind, map_x, map_z);
    int wanted = RSCache_ArchiveNameHashDat2(name);
    int table_id = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_MAPS);
    if( table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT || !disk->tables[table_id] )
        return -1;
    const struct RSCache_ReferenceTable* table = disk->tables[table_id];
    for( int i = 0; i < table->archive_count; i++ )
        if( table->archives[i].index >= 0 && table->archives[i].identifier == wanted )
            return table->archives[i].index;
    return -1;
}

static int
counts_preserve_alloc(
    struct IntCounts* counts,
    const char* path,
    const char* symbol_prefix,
    int allocation_base)
{
    FILE* in = fopen(path, "rb");
    if( !in )
        return errno == ENOENT;

    struct IntCount* ordered = calloc((size_t)counts->n, sizeof(*ordered));
    unsigned char* used = calloc((size_t)counts->n, 1);
    if( !ordered || !used )
    {
        free(ordered);
        free(used);
        fclose(in);
        return 0;
    }

    char line[2048];
    int preserved = 0;
    size_t prefix_len = strlen(symbol_prefix);
    while( fgets(line, sizeof(line), in) )
    {
        char* end = NULL;
        long destination = strtol(line, &end, 10);
        if( end == line || *end != '=' ||
            strncmp(end + 1, symbol_prefix, prefix_len) != 0 )
            continue;
        char* source_end = NULL;
        long source = strtol(end + 1 + prefix_len, &source_end, 10);
        if( source_end == end + 1 + prefix_len ||
            (*source_end != '\0' && *source_end != '\r' && *source_end != '\n') )
            continue;
        int slot = (int)destination - allocation_base;
        int source_index = counts_index(counts, (int)source);
        if( slot < 0 || slot >= counts->n || source_index < 0 || used[source_index] ||
            ordered[slot].count != 0 )
        {
            fprintf(stderr, "map_port: invalid or conflicting floor allocation in %s\n", path);
            free(ordered);
            free(used);
            fclose(in);
            return 0;
        }
        ordered[slot] = counts->v[source_index];
        used[source_index] = 1;
        preserved++;
    }
    fclose(in);

    /* Existing allocations must occupy a contiguous prefix. New source ids,
     * already sorted by the inventory pass, append after that prefix. */
    for( int i = 0; i < preserved; i++ )
    {
        if( ordered[i].count == 0 )
        {
            fprintf(stderr, "map_port: floor allocation gap in %s\n", path);
            free(ordered);
            free(used);
            return 0;
        }
    }
    int append = preserved;
    for( int i = 0; i < counts->n; i++ )
        if( !used[i] )
            ordered[append++] = counts->v[i];
    if( append != counts->n )
    {
        free(ordered);
        free(used);
        return 0;
    }
    memcpy(counts->v, ordered, (size_t)counts->n * sizeof(*counts->v));
    free(ordered);
    free(used);
    return 1;
}

static int
parse_squares(const char* text, struct Square* squares, int* out_count)
{
    char* copy = strdup(text);
    char* save = NULL;
    char* token = strtok_r(copy, ",", &save);
    int count = 0;
    while( token )
    {
        int x = -1, z = -1;
        char tail = '\0';
        if( count == MAP_PORT_MAX_SQUARES || sscanf(token, "%d_%d%c", &x, &z, &tail) != 2 ||
            x < 0 || x > 255 || z < 0 || z > 255 )
        {
            fprintf(stderr, "map_port: invalid square: %s\n", token);
            free(copy);
            return 0;
        }
        squares[count].x = x;
        squares[count].z = z;
        count++;
        token = strtok_r(NULL, ",", &save);
    }
    free(copy);
    *out_count = count;
    return count > 0;
}

static int
load_record(
    struct Tool_Dat2Cache* cache,
    enum RSCache_Type type,
    int id,
    uint8_t** out_data,
    int* out_size)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&cache->profile, type);
    int table;
    int archive_id;
    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(cache->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = addr.group;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(cache->disk, addr.table);
        archive_id = id >> addr.group_shift;
    }
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(cache->disk, table, archive_id);
    if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(cache->disk, archive) )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    int found = 0;
    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = archive->file_ids ? archive->file_ids[i] : i;
        int global = addr.group_shift ? ((archive_id << addr.group_shift) | file_id) : file_id;
        if( global != id || files->file_sizes[i] <= 0 )
            continue;
        *out_data = malloc((size_t)files->file_sizes[i]);
        if( *out_data )
        {
            memcpy(*out_data, files->files[i], (size_t)files->file_sizes[i]);
            *out_size = files->file_sizes[i];
            found = 1;
        }
        break;
    }
    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return found;
}

static int
load_ledger(const char* path, struct LocMap** out_map, int* out_count)
{
    FILE* file = fopen(path, "rb");
    if( !file )
    {
        fprintf(stderr, "map_port: cannot open ledger %s: %s\n", path, strerror(errno));
        return 0;
    }
    struct LocMap* map = NULL;
    int count = 0, cap = 0;
    char line[2048];
    while( fgets(line, sizeof(line), file) )
    {
        char* fields[7] = { 0 };
        int field_count = 0;
        char* save = NULL;
        for( char* token = strtok_r(line, "\t\r\n", &save); token && field_count < 7;
             token = strtok_r(NULL, "\t\r\n", &save) )
            fields[field_count++] = token;
        if( field_count < 4 || strcmp(fields[0], "loc") != 0 )
            continue;
        if( count == cap )
        {
            int next = cap ? cap * 2 : 128;
            void* grown = realloc(map, (size_t)next * sizeof(*map));
            if( !grown )
            {
                free(map);
                fclose(file);
                return 0;
            }
            map = grown;
            cap = next;
        }
        map[count].source = atoi(fields[1]);
        map[count].dest = atoi(fields[3]);
        count++;
    }
    fclose(file);
    *out_map = map;
    *out_count = count;
    return 1;
}

static int
header_column(char** fields, int field_count, const char* a, const char* b, const char* c)
{
    for( int i = 0; i < field_count; i++ )
        if( strcmp(fields[i], a) == 0 || strcmp(fields[i], b) == 0 || strcmp(fields[i], c) == 0 )
            return i;
    return -1;
}

/* A dedicated `source_material<TAB>dest_texture` table is preferred, but the
 * reader also accepts those columns inside a wider provenance TSV. */
static int
load_material_ledger(const char* path, struct LocMap** out_map, int* out_count)
{
    FILE* file = fopen(path, "rb");
    if( !file )
    {
        fprintf(stderr, "map_port: cannot open material ledger %s: %s\n", path,
                strerror(errno));
        return 0;
    }
    struct LocMap* map = NULL;
    int count = 0, cap = 0;
    int source_column = -1, dest_column = -1;
    char line[4096];
    int line_number = 0;
    while( fgets(line, sizeof(line), file) )
    {
        line_number++;
        char* fields[32] = { 0 };
        int field_count = 0;
        char* save = NULL;
        for( char* token = strtok_r(line, "\t\r\n", &save); token && field_count < 32;
             token = strtok_r(NULL, "\t\r\n", &save) )
            fields[field_count++] = token;
        if( field_count == 0 || fields[0][0] == '#' )
            continue;
        if( source_column < 0 )
        {
            source_column = header_column(
                fields, field_count, "source_material", "source_id", "source");
            dest_column = header_column(
                fields, field_count, "dest_texture", "dest_id", "destination");
            if( source_column >= 0 && dest_column >= 0 )
                continue;
            source_column = 0;
            dest_column = 1;
        }
        if( source_column >= field_count || dest_column >= field_count )
        {
            fprintf(stderr, "map_port: %s:%d: short material ledger row\n", path,
                    line_number);
            free(map);
            fclose(file);
            return 0;
        }
        char* source_end = NULL;
        char* dest_end = NULL;
        long source = strtol(fields[source_column], &source_end, 10);
        long dest = strtol(fields[dest_column], &dest_end, 10);
        if( !source_end || *source_end || !dest_end || *dest_end || source < 0 || dest < 0 ||
            source > 0x7fffffffL || dest > 0x7fffffffL )
        {
            fprintf(stderr, "map_port: %s:%d: invalid material mapping\n", path,
                    line_number);
            free(map);
            fclose(file);
            return 0;
        }
        if( count == cap )
        {
            int next = cap ? cap * 2 : 128;
            void* grown = realloc(map, (size_t)next * sizeof(*map));
            if( !grown )
            {
                free(map);
                fclose(file);
                return 0;
            }
            map = grown;
            cap = next;
        }
        if( loc_map_find(map, count, (int)source) >= 0 )
        {
            fprintf(stderr, "map_port: %s:%d: duplicate source material %ld\n", path,
                    line_number, source);
            free(map);
            fclose(file);
            return 0;
        }
        map[count].source = (int)source;
        map[count].dest = (int)dest;
        count++;
    }
    fclose(file);
    *out_map = map;
    *out_count = count;
    return 1;
}

static int
loc_map_find(const struct LocMap* map, int count, int source)
{
    for( int i = 0; i < count; i++ )
        if( map[i].source == source )
            return map[i].dest;
    return -1;
}

static int
write_inventory(
    struct Tool_Dat2Cache* cache,
    const char* path,
    const struct Square* squares,
    int square_count,
    const struct IntCounts* locs,
    const struct IntCounts* underlays,
    const struct IntCounts* overlays)
{
    if( !ensure_parent(path) )
        return 0;
    FILE* out = fopen(path, "wb");
    if( !out )
        return 0;
    fprintf(out, "kind\tid\treferences\tdetail\n");
    for( int i = 0; i < square_count; i++ )
    {
        fprintf(out,
                "square\t%d_%d\t%d\ttiles=%d;locs=%d;terrain-archive=%d;"
                "loc-archive=%d;xtea=%d,%d,%d,%d\n",
                squares[i].x, squares[i].z, squares[i].locs->locs_count,
                squares[i].nonempty_tiles, squares[i].locs->locs_count,
                squares[i].terrain_archive, squares[i].loc_archive,
                squares[i].xtea[0], squares[i].xtea[1], squares[i].xtea[2],
                squares[i].xtea[3]);
        struct IntCounts per_square = { 0 };
        for( int spawn = 0; spawn < squares[i].locs->locs_count; spawn++ )
        {
            if( !counts_add(&per_square, squares[i].locs->locs[spawn].loc_id) )
            {
                free(per_square.v);
                fclose(out);
                return 0;
            }
        }
        qsort(per_square.v, (size_t)per_square.n, sizeof(*per_square.v), count_compare);
        for( int loc = 0; loc < per_square.n; loc++ )
            fprintf(out, "square-loc\t%d_%d:%d\t%d\tsource-loc-id=%d\n", squares[i].x,
                    squares[i].z, per_square.v[loc].id, per_square.v[loc].count,
                    per_square.v[loc].id);
        free(per_square.v);
        for( int spawn = 0; spawn < squares[i].locs->locs_count; spawn++ )
        {
            const struct RSCache_MapLoc* loc = &squares[i].locs->locs[spawn];
            fprintf(out,
                    "placement\t%d_%d:%d\t1\tloc=%d;level=%d;x=%d;z=%d;shape=%d;angle=%d\n",
                    squares[i].x, squares[i].z, spawn, loc->loc_id,
                    loc->chunk_pos_level, loc->chunk_pos_x, loc->chunk_pos_z,
                    loc->shape_select, loc->orientation);
        }
    }
    for( int i = 0; i < locs->n; i++ )
        fprintf(out, "loc\t%d\t%d\tconfig-id\n", locs->v[i].id, locs->v[i].count);
    for( int i = 0; i < underlays->n; i++ )
    {
        uint8_t* data = NULL;
        int size = 0;
        if( !load_record(cache, RSCACHE_TYPE_UNDERLAY, underlays->v[i].id, &data, &size) )
        {
            fclose(out);
            return 0;
        }
        struct RSCache_Dat2ConfigUnderlay floor;
        RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
            &floor, (char*)data, size, RSCache_Dat2ConfigFloFlags(&cache->profile));
        fprintf(out,
                "underlay\t%d\t%d\twire-id=%d;colour=0x%06X;rs2-texture=%d;rs2-scale=%d\n",
                underlays->v[i].id, underlays->v[i].count, underlays->v[i].id + 1,
                floor.rgb_color & 0xFFFFFF, floor.rs2_texture, floor.rs2_scale);
        free(data);
    }
    for( int i = 0; i < overlays->n; i++ )
    {
        uint8_t* data = NULL;
        int size = 0;
        if( !load_record(cache, RSCACHE_TYPE_OVERLAY, overlays->v[i].id, &data, &size) )
        {
            fclose(out);
            return 0;
        }
        struct RSCache_Dat2ConfigOverlay floor;
        RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
            &floor, (char*)data, size, RSCache_Dat2ConfigFloFlags(&cache->profile));
        fprintf(out,
                "overlay\t%d\t%d\twire-id=%d;colour=0x%06X;texture=%d;blend=0x%06X;"
                "hide=%d;rs2-scale=%d;water=0x%06X;water-scale=%d;water-intensity=%d;"
                "secondary-texture=%d\n",
                overlays->v[i].id, overlays->v[i].count, overlays->v[i].id + 1,
                floor.rgb_color & 0xFFFFFF, floor.texture,
                floor.secondary_rgb_color & 0xFFFFFF, floor.hide_underlay ? 1 : 0,
                floor.rs2_scale, floor.rs2_water_colour & 0xFFFFFF, floor.rs2_water_scale,
                floor.rs2_water_intensity, floor.rs2_secondary_texture);
        RSCache_Dat2ConfigOverlayFreeInplace(&floor);
        free(data);
    }
    fclose(out);
    return 1;
}

static int
write_terrain(
    const char* path,
    const struct Square* square,
    const struct IntCounts* underlays,
    const struct IntCounts* overlays,
    int underlay_base,
    int overlay_base)
{
    if( !ensure_parent(path) )
        return 0;
    FILE* out = fopen(path, "wb");
    if( !out )
        return 0;
    fprintf(out, "// RS727 m%d_%d; profile-decoded and widened for OSRS239.\n", square->x,
            square->z);
    fprintf(out, "==== MAP ====\n");
    if( square->terrain->trailing_size > 0 )
    {
        fprintf(out, "trailing=");
        for( int i = 0; i < square->terrain->trailing_size; i++ )
            fprintf(out, "%02x", square->terrain->trailing[i]);
        fputc('\n', out);
    }
    for( int level = 0; level < RSCACHE_MAP_TERRAIN_LEVELS; level++ )
    {
        for( int x = 0; x < RSCACHE_MAP_TERRAIN_X; x++ )
        {
            for( int z = 0; z < RSCACHE_MAP_TERRAIN_Z; z++ )
            {
                const struct RSCache_MapFloor* tile =
                    &square->terrain->tiles_xyz[RSCACHE_MAP_TILE_COORD(x, z, level)];
                if( !tile->height_authored && !tile->attr_opcode && !tile->settings &&
                    !tile->underlay_id )
                    continue;
                fprintf(out, "%d %d %d:", level, x, z);
                if( tile->height_authored )
                    fprintf(out, " h%d", tile->authored_height);
                if( tile->attr_opcode )
                {
                    int source = (int)tile->overlay_id - 1;
                    int index = counts_index(overlays, source);
                    if( index < 0 )
                    {
                        fclose(out);
                        return 0;
                    }
                    fprintf(out, " o%d;%d;%d", overlay_base + index + 1, tile->shape,
                            tile->rotation);
                }
                if( tile->settings )
                    fprintf(out, " f%d", tile->settings);
                if( tile->underlay_id )
                {
                    int source = (int)tile->underlay_id - 1;
                    int index = counts_index(underlays, source);
                    if( index < 0 )
                    {
                        fclose(out);
                        return 0;
                    }
                    fprintf(out, " u%d", underlay_base + index + 1);
                }
                fputc('\n', out);
            }
        }
    }
    fclose(out);
    return 1;
}

static int
write_locs(
    const char* path,
    const struct Square* square,
    const struct LocMap* loc_map,
    int loc_map_count)
{
    if( !ensure_parent(path) )
        return 0;
    FILE* out = fopen(path, "wb");
    if( !out )
        return 0;
    fprintf(out, "// RS727 l%d_%d; all ids rewritten through the import ledger.\n",
            square->x, square->z);
    fprintf(out, "==== LOC ====\n");
    for( int i = 0; i < square->locs->locs_count; i++ )
    {
        const struct RSCache_MapLoc* loc = &square->locs->locs[i];
        int dest = loc_map_find(loc_map, loc_map_count, loc->loc_id);
        if( dest < 0 )
        {
            fprintf(stderr, "map_port: source loc %d has no ledger mapping\n", loc->loc_id);
            fclose(out);
            return 0;
        }
        fprintf(out, "%d %d %d: %d %d %d\n", loc->chunk_pos_level, loc->chunk_pos_x,
                loc->chunk_pos_z, dest, loc->shape_select, loc->orientation);
    }
    fclose(out);
    return 1;
}

static int
write_floor_configs(
    struct Tool_Dat2Cache* cache,
    const char* out_root,
    const struct IntCounts* underlays,
    const struct IntCounts* overlays,
    int underlay_base,
    int overlay_base,
    const struct LocMap* material_map,
    int material_map_count)
{
    char path[MAP_PORT_PATH];
    snprintf(path, sizeof(path), "%s/configs/rs2012.underlay", out_root);
    if( !ensure_parent(path) )
        return 0;
    FILE* underlay_out = fopen(path, "wb");
    if( !underlay_out )
        return 0;
    for( int i = 0; i < underlays->n; i++ )
    {
        uint8_t* data = NULL;
        int size = 0;
        if( !load_record(cache, RSCACHE_TYPE_UNDERLAY, underlays->v[i].id, &data, &size) )
        {
            fprintf(stderr, "map_port: missing underlay config %d\n", underlays->v[i].id);
            fclose(underlay_out);
            return 0;
        }
        struct RSCache_Dat2ConfigUnderlay floor;
        RSCache_Dat2ConfigUnderlayDecodeInplaceFlags(
            &floor, (char*)data, size, RSCache_Dat2ConfigFloFlags(&cache->profile));
        fprintf(underlay_out, "[rs2012_underlay_%d]\n", underlays->v[i].id);
        if( floor.rgb_color )
            fprintf(underlay_out, "colour=0x%06X\n", floor.rgb_color & 0xFFFFFF);
        fprintf(underlay_out, "\n");
        free(data);
    }
    fclose(underlay_out);

    snprintf(path, sizeof(path), "%s/configs/rs2012.overlay", out_root);
    FILE* overlay_out = fopen(path, "wb");
    if( !overlay_out )
        return 0;
    for( int i = 0; i < overlays->n; i++ )
    {
        uint8_t* data = NULL;
        int size = 0;
        if( !load_record(cache, RSCACHE_TYPE_OVERLAY, overlays->v[i].id, &data, &size) )
        {
            fprintf(stderr, "map_port: missing overlay config %d\n", overlays->v[i].id);
            fclose(overlay_out);
            return 0;
        }
        struct RSCache_Dat2ConfigOverlay floor;
        RSCache_Dat2ConfigOverlayDecodeInplaceFlags(
            &floor, (char*)data, size, RSCache_Dat2ConfigFloFlags(&cache->profile));
        fprintf(overlay_out, "[rs2012_overlay_%d]\n", overlays->v[i].id);
        if( floor.rgb_color )
            fprintf(overlay_out, "colour=0x%06X\n", floor.rgb_color & 0xFFFFFF);
        /* RS2's texture operand is u16; every retained reference must be
         * explicitly allocated into OSRS's u8 material band. Never reuse a raw
         * source id merely because it happens to fit. */
        if( floor.texture >= 0 )
        {
            int mapped = loc_map_find(material_map, material_map_count, floor.texture);
            if( mapped < 0 )
            {
                fprintf(stderr, "map_port: overlay %d material %d has no mapping\n",
                        overlays->v[i].id, floor.texture);
                RSCache_Dat2ConfigOverlayFreeInplace(&floor);
                free(data);
                fclose(overlay_out);
                return 0;
            }
            if( mapped > 255 )
            {
                fprintf(stderr,
                        "map_port: overlay %d material %d maps outside the u8 texture band (%d)\n",
                        overlays->v[i].id, floor.texture, mapped);
                RSCache_Dat2ConfigOverlayFreeInplace(&floor);
                free(data);
                fclose(overlay_out);
                return 0;
            }
            fprintf(overlay_out, "texture=%d\n", mapped);
        }
        if( !floor.hide_underlay )
            fprintf(overlay_out, "hideunderlay=no\n");
        if( floor.secondary_rgb_color >= 0 )
            fprintf(overlay_out, "blendcolour=0x%06X\n",
                    floor.secondary_rgb_color & 0xFFFFFF);
        else if( floor.rs2_water_colour )
            fprintf(overlay_out, "blendcolour=0x%06X\n", floor.rs2_water_colour & 0xFFFFFF);
        fprintf(overlay_out, "\n");
        RSCache_Dat2ConfigOverlayFreeInplace(&floor);
        free(data);
    }
    fclose(overlay_out);

    const char* kinds[] = { "underlay", "overlay" };
    const struct IntCounts* lists[] = { underlays, overlays };
    int bases[] = { underlay_base, overlay_base };
    for( int kind = 0; kind < 2; kind++ )
    {
        snprintf(path, sizeof(path), "%s/pack/%s.alloc", out_root, kinds[kind]);
        if( !ensure_parent(path) )
            return 0;
        FILE* alloc = fopen(path, "wb");
        if( !alloc )
            return 0;
        for( int i = 0; i < lists[kind]->n; i++ )
            fprintf(alloc, "%d=rs2012_%s_%d\n", bases[kind] + i, kinds[kind],
                    lists[kind]->v[i].id);
        fclose(alloc);
        snprintf(path, sizeof(path), "%s/pack/%s.client", out_root, kinds[kind]);
        FILE* client = fopen(path, "wb");
        if( !client )
            return 0;
        for( int i = 0; i < lists[kind]->n; i++ )
            fprintf(client, "rs2012_%s_%d\n", kinds[kind], lists[kind]->v[i].id);
        fclose(client);
    }
    return 1;
}

static int
write_map_metadata(
    const char* out_root,
    const char* base_tree,
    const struct Square* squares,
    int square_count)
{
    char path[MAP_PORT_PATH];
    snprintf(path, sizeof(path), "%s/pack/5_maps.pack", out_root);
    if( !ensure_parent(path) )
        return 0;
    FILE* pack = fopen(path, "wb");
    if( !pack )
        return 0;
    for( int i = 0; i < square_count; i++ )
        fprintf(pack, "%d=m%d_%d\n", (squares[i].x << 8) | squares[i].z,
                squares[i].x, squares[i].z);
    fclose(pack);

    for( int i = 0; i < square_count; i++ )
    {
        snprintf(path, sizeof(path), "%s/maps/m%d_%d.filepack", out_root, squares[i].x,
                 squares[i].z);
        if( !ensure_parent(path) )
            return 0;
        FILE* filepack = fopen(path, "wb");
        if( !filepack )
            return 0;
        fprintf(filepack, "0=m%d_%d.jm2\n1=m%d_%d.jl2\n", squares[i].x,
                squares[i].z, squares[i].x, squares[i].z);
        /* OSRS 239 squares may carry auxiliary files 2..N. The historical
         * terrain/loc pair replaces only members 0/1, so retain the base index
         * rows. Their binaries remain in the staged base tree. */
        if( base_tree )
        {
            char base_path[MAP_PORT_PATH];
            snprintf(base_path, sizeof(base_path), "%s/maps/m%d_%d.filepack", base_tree,
                     squares[i].x, squares[i].z);
            FILE* base = fopen(base_path, "rb");
            if( base )
            {
                char line[2048];
                while( fgets(line, sizeof(line), base) )
                {
                    char* end = NULL;
                    long id = strtol(line, &end, 10);
                    if( end != line && *end == '=' && id >= 2 )
                        fputs(line, filepack);
                }
                fclose(base);
            }
        }
        fclose(filepack);
    }
    return 1;
}

int
main(int argc, char** argv)
{
    struct Options opt;
    memset(&opt, 0, sizeof(opt));
    opt.underlay_base = 500;
    opt.overlay_base = 1000;
    for( int i = 1; i < argc; i++ )
    {
#define VALUE(name, field)                                                                          \
        if( strcmp(argv[i], name) == 0 && i + 1 < argc )                                            \
        {                                                                                            \
            opt.field = argv[++i];                                                                   \
            continue;                                                                                \
        }
        VALUE("--rev", rev)
        VALUE("--cache", cache)
        VALUE("--squares", squares)
        VALUE("--inventory", inventory)
        VALUE("--ledger", ledger)
        VALUE("--material-ledger", material_ledger)
        VALUE("--out", out)
        VALUE("--base-tree", base_tree)
#undef VALUE
        if( strcmp(argv[i], "--underlay-base") == 0 && i + 1 < argc )
        {
            opt.underlay_base = atoi(argv[++i]);
            continue;
        }
        if( strcmp(argv[i], "--overlay-base") == 0 && i + 1 < argc )
        {
            opt.overlay_base = atoi(argv[++i]);
            continue;
        }
        if( strcmp(argv[i], "--apply") == 0 )
        {
            opt.apply = 1;
            continue;
        }
        usage(argv[0]);
        return 2;
    }
    if( !opt.rev || !opt.cache || !opt.squares || !opt.inventory ||
        (opt.apply && (!opt.ledger || !opt.material_ledger || !opt.out)) ||
        opt.underlay_base < 0 ||
        opt.overlay_base < 0 )
    {
        usage(argv[0]);
        return 2;
    }

    struct RSCache profile;
    if( !tool_resolve_profile(opt.rev, NULL, NULL, NULL, NULL, &profile) )
        return 1;
    struct Tool_Dat2Cache cache;
    if( !tool_dat2_open(opt.cache, &profile, &cache) )
        return 1;
    char xtea_path[MAP_PORT_PATH];
    snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", opt.cache);
    if( RSCache_MapLocsEncrypted(&profile) && RSCache_XteaConfigLoadKeys(xtea_path) < 0 )
    {
        fprintf(stderr, "map_port: cannot load XTEA keys: %s\n", xtea_path);
        tool_dat2_close(&cache);
        return 1;
    }

    struct Square squares[MAP_PORT_MAX_SQUARES];
    memset(squares, 0, sizeof(squares));
    int square_count = 0;
    if( !parse_squares(opt.squares, squares, &square_count) )
    {
        tool_dat2_close(&cache);
        return 1;
    }

    struct IntCounts locs = { 0 }, underlays = { 0 }, overlays = { 0 };
    int ok = 1;
    for( int i = 0; ok && i < square_count; i++ )
    {
        squares[i].terrain_archive =
            named_map_archive_id(cache.disk, "m", squares[i].x, squares[i].z);
        squares[i].loc_archive =
            named_map_archive_id(cache.disk, "l", squares[i].x, squares[i].z);
        int map_table = RSCache_Dat2DiskTableId(cache.disk, RSCACHE_DAT2_TABLE_MAPS);
        int32_t* key = RSCache_XteaConfigFindKey(map_table, squares[i].loc_archive);
        if( squares[i].terrain_archive < 0 || squares[i].loc_archive < 0 || !key )
        {
            fprintf(stderr, "map_port: source archive/key metadata missing for %d_%d\n",
                    squares[i].x, squares[i].z);
            ok = 0;
            break;
        }
        memcpy(squares[i].xtea, key, sizeof(squares[i].xtea));
        squares[i].terrain =
            RSCache_MapTerrainNewFromCache(cache.disk, squares[i].x, squares[i].z);
        squares[i].locs = RSCache_MapLocsNewFromCache(cache.disk, squares[i].x, squares[i].z);
        if( !squares[i].terrain || !squares[i].locs )
        {
            fprintf(stderr, "map_port: failed to load square %d_%d\n", squares[i].x,
                    squares[i].z);
            ok = 0;
            break;
        }
        for( int tile = 0; tile < RSCACHE_CHUNK_TILE_COUNT; tile++ )
        {
            const struct RSCache_MapFloor* floor = &squares[i].terrain->tiles_xyz[tile];
            if( floor->height_authored || floor->attr_opcode || floor->settings ||
                floor->underlay_id )
                squares[i].nonempty_tiles++;
            if( floor->attr_opcode )
                ok = ok && counts_add(&overlays, (int)floor->overlay_id - 1);
            if( floor->underlay_id )
                ok = ok && counts_add(&underlays, (int)floor->underlay_id - 1);
        }
        for( int spawn = 0; ok && spawn < squares[i].locs->locs_count; spawn++ )
            ok = counts_add(&locs, squares[i].locs->locs[spawn].loc_id);
    }
    qsort(locs.v, (size_t)locs.n, sizeof(*locs.v), count_compare);
    qsort(underlays.v, (size_t)underlays.n, sizeof(*underlays.v), count_compare);
    qsort(overlays.v, (size_t)overlays.n, sizeof(*overlays.v), count_compare);

    if( ok && opt.apply && opt.out )
    {
        char alloc_path[MAP_PORT_PATH];
        snprintf(alloc_path, sizeof(alloc_path), "%s/pack/underlay.alloc", opt.out);
        ok = counts_preserve_alloc(
            &underlays, alloc_path, "rs2012_underlay_", opt.underlay_base);
        snprintf(alloc_path, sizeof(alloc_path), "%s/pack/overlay.alloc", opt.out);
        if( ok )
            ok = counts_preserve_alloc(
                &overlays, alloc_path, "rs2012_overlay_", opt.overlay_base);
    }

    if( ok )
        ok = write_inventory(
            &cache, opt.inventory, squares, square_count, &locs, &underlays, &overlays);
    printf("map_port: %d squares, %d loc ids, %d underlays, %d overlays\n", square_count,
           locs.n, underlays.n, overlays.n);

    if( ok && opt.apply )
    {
        struct LocMap* loc_map = NULL;
        int loc_map_count = 0;
        struct LocMap* material_map = NULL;
        int material_map_count = 0;
        ok = load_ledger(opt.ledger, &loc_map, &loc_map_count);
        if( ok && opt.material_ledger )
            ok = load_material_ledger(
                opt.material_ledger, &material_map, &material_map_count);
        for( int i = 0; ok && i < locs.n; i++ )
        {
            if( loc_map_find(loc_map, loc_map_count, locs.v[i].id) < 0 )
            {
                fprintf(stderr,
                        "map_port: ledger is incomplete; append loc %d to the import manifest\n",
                        locs.v[i].id);
                ok = 0;
            }
        }
        for( int i = 0; ok && i < square_count; i++ )
        {
            char path[MAP_PORT_PATH];
            snprintf(path, sizeof(path), "%s/maps/m%d_%d.jm2", opt.out, squares[i].x,
                     squares[i].z);
            ok = write_terrain(path, &squares[i], &underlays, &overlays,
                               opt.underlay_base, opt.overlay_base);
            snprintf(path, sizeof(path), "%s/maps/m%d_%d.jl2", opt.out, squares[i].x,
                     squares[i].z);
            if( ok )
                ok = write_locs(path, &squares[i], loc_map, loc_map_count);
        }
        if( ok )
            ok = write_floor_configs(&cache, opt.out, &underlays, &overlays,
                                     opt.underlay_base, opt.overlay_base,
                                     material_map, material_map_count);
        if( ok )
            ok = write_map_metadata(opt.out, opt.base_tree, squares, square_count);
        free(loc_map);
        free(material_map);
    }

    for( int i = 0; i < square_count; i++ )
    {
        RSCache_MapTerrainFree(squares[i].terrain);
        RSCache_MapLocsFree(squares[i].locs);
    }
    free(locs.v);
    free(underlays.v);
    free(overlays.v);
    tool_dat2_close(&cache);
    if( ok && opt.apply )
        printf("map_port: wrote isolated map lane %s\n", opt.out);
    return ok ? 0 : 1;
}
