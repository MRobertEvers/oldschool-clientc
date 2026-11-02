/**
 * The asset catalog and the square atlas (torirsmaped/torirs_maped_catalog.h).
 *
 *   catalog_test <content-dir> <scratch-dir>
 *
 * Part A, over the real content tree, READ ONLY (the catalog API directly:
 * a ToriRSMapEd would take the tree's lock, which writes a file):
 *   - builds the index and prints how long it took, then refreshes with
 *     nothing changed and prints that;
 *   - every name in every pack/<t>.pack, pack/<ns>.alloc,
 *     configs/all.<type>.compack and interfaces/<name>.compack is found by
 *     exact search with its id;
 *   - the atlas holds every square on disk, the surface and a dungeon are
 *     claimed by the right world-map areas, the instance squares are not.
 *
 * Part B, a small tree written under <scratch-dir>, through the embedded
 * server and the client binding: CATALOG_QUERY paging and filters,
 * CATALOG_RECORD provenance (an overlay restating a cache field),
 * CATALOG_SQUARES, and the atlas broadcast after DOC_CREATE and Save.
 *
 * Part C, the same tree through the API: an edited overlay, a new square and
 * a deleted one are each picked up by the mtime refresh, and only they are
 * re-parsed.
 */

#include "editor/editor_host.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"

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

static int
write_text(
    const char* path,
    const char* text)
{
    FILE* f = fopen(path, "wb");
    if( !f )
        return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

static int
copy_file(
    const char* from,
    const char* to)
{
    size_t size;
    char* data = slurp(from, &size);
    FILE* f;
    if( !data )
        return 0;
    f = fopen(to, "wb");
    if( !f )
    {
        free(data);
        return 0;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    free(data);
    return 1;
}

static void
make_dirs(const char* path)
{
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", path);
    for( char* p = buf + 1; *p; p++ )
    {
        if( *p == '/' )
        {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    mkdir(buf, 0755);
}

/* ------------------------------------------------------------------ */
/* Part A                                                              */
/* ------------------------------------------------------------------ */

/** The kind a pack table's names belong to — written out again here, not
 *  borrowed from the catalog, so a wrong mapping there fails here. */
static uint32_t
pack_table_kind(const char* stem)
{
    static const struct
    {
        const char* table;
        uint32_t kind;
    } tables[] = {
        { "0_animations", TORIRSMAPED_CATALOG_FRAMESET },
        { "1_skeletons", TORIRSMAPED_CATALOG_SKELETON },
        { "3_interfaces", TORIRSMAPED_CATALOG_INTERFACE },
        { "4_soundeffects", TORIRSMAPED_CATALOG_SYNTH },
        { "5_maps", TORIRSMAPED_CATALOG_SQUARE },
        { "6_musictracks", TORIRSMAPED_CATALOG_SONG },
        { "7_models", TORIRSMAPED_CATALOG_MODEL },
        { "8_sprites", TORIRSMAPED_CATALOG_SPRITE },
        { "9_textures", TORIRSMAPED_CATALOG_TEXTURE },
        { "11_musicjingles", TORIRSMAPED_CATALOG_JINGLE },
        { "12_clientscripts", TORIRSMAPED_CATALOG_CS2 },
        { "13_fonts", TORIRSMAPED_CATALOG_FONT },
        { "category", TORIRSMAPED_CATALOG_CATEGORY },
        { "stat", TORIRSMAPED_CATALOG_STAT },
    };
    for( size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++ )
    {
        if( strcmp(tables[i].table, stem) == 0 )
            return tables[i].kind;
    }
    return TORIRSMAPED_CATALOG_PACK;
}

static int
config_kind(const char* name)
{
    for( uint32_t kind = TORIRSMAPED_CATALOG_LOC; kind <= TORIRSMAPED_CATALOG_MAPELEMENT; kind++ )
    {
        if( strcmp(ToriRSMapEdCatalog_KindName(kind), name) == 0 )
            return (int)kind;
    }
    return -1;
}

struct name_tally
{
    long names;
    long missing;
    long files;
};

/** Exact-search every `id=name` line of one file under `kind`, the name
 *  given `prefix` ("bank:" for an interface's components). */
static void
check_names_in(
    struct ToriRSMapEdCatalog* catalog,
    const char* path,
    uint32_t kind,
    const char* prefix,
    struct name_tally* tally)
{
    size_t size;
    char* text = slurp(path, &size);
    char* p;
    int records[64];

    if( !text )
    {
        check(0, "a pack file could be read");
        return;
    }
    tally->files++;
    for( p = text; *p; )
    {
        char* nl = strchr(p, '\n');
        char* eq;
        char* end;
        if( nl )
            *nl = '\0';
        eq = strchr(p, '=');
        if( p[0] != '/' && eq && eq > p )
        {
            char* digits = p;
            long id;
            int ok = 1;
            if( *digits == '-' )
                digits++;
            for( char* c = digits; c < eq; c++ )
                ok &= *c >= '0' && *c <= '9';
            end = eq + 1;
            while( *end && *end != ' ' && *end != '\t' && *end != '\r' )
                end++;
            if( ok && end > eq + 1 )
            {
                char name[512];
                int found = 0;
                int count;
                id = strtol(p, NULL, 10);
                snprintf(name, sizeof(name), "%s%.*s", prefix, (int)(end - (eq + 1)), eq + 1);
                count = ToriRSMapEdCatalog_Query(
                    catalog, kind, name, TORIRSMAPED_CATALOG_EXACT, records, 64);
                for( int i = 0; i < count && i < 64 && !found; i++ )
                {
                    struct ToriRSMapEdCatalogEntry entry;
                    ToriRSMapEdCatalog_Entry(catalog, records[i], &entry);
                    found = entry.kind == kind && strcmp(entry.name, name) == 0 && entry.id == id;
                }
                tally->names++;
                if( !found )
                {
                    if( tally->missing < 10 )
                        fprintf(stderr, "FAIL: %s: %s %s (id %ld) not found by exact search\n",
                            path, ToriRSMapEdCatalog_KindName(kind), name, id);
                    tally->missing++;
                }
            }
        }
        p = nl ? nl + 1 : p + strlen(p);
    }
    free(text);
}

static int
ends_with(
    const char* s,
    const char* suffix)
{
    size_t a = strlen(s);
    size_t b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static void
check_every_pack_name(
    struct ToriRSMapEdCatalog* catalog,
    const char* content)
{
    struct name_tally packs = { 0, 0, 0 };
    struct name_tally allocs = { 0, 0, 0 };
    struct name_tally compacks = { 0, 0, 0 };
    struct name_tally ifaces = { 0, 0, 0 };
    char dir_path[1024];
    DIR* dir;
    struct dirent* entry;
    char line[256];

    snprintf(dir_path, sizeof(dir_path), "%s/pack", content);
    dir = opendir(dir_path);
    check(dir != NULL, "pack/ opens");
    while( dir && (entry = readdir(dir)) != NULL )
    {
        char path[2048];
        char stem[256];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if( ends_with(entry->d_name, ".pack") )
        {
            snprintf(stem, sizeof(stem), "%.*s", (int)(strlen(entry->d_name) - 5), entry->d_name);
            check_names_in(catalog, path, pack_table_kind(stem), "", &packs);
        }
        else if( ends_with(entry->d_name, ".alloc") )
        {
            int kind;
            snprintf(stem, sizeof(stem), "%.*s", (int)(strlen(entry->d_name) - 6), entry->d_name);
            kind = config_kind(stem);
            check(kind >= 0, "every pack/<ns>.alloc names a config kind");
            if( kind >= 0 )
                check_names_in(catalog, path, (uint32_t)kind, "", &allocs);
        }
    }
    if( dir )
        closedir(dir);

    snprintf(dir_path, sizeof(dir_path), "%s/configs", content);
    dir = opendir(dir_path);
    check(dir != NULL, "configs/ opens");
    while( dir && (entry = readdir(dir)) != NULL )
    {
        char path[2048];
        char type[256];
        int kind;
        if( strncmp(entry->d_name, "all.", 4) != 0 || !ends_with(entry->d_name, ".compack") )
            continue;
        snprintf(type, sizeof(type), "%.*s", (int)(strlen(entry->d_name) - 12), entry->d_name + 4);
        kind = config_kind(type);
        check(kind >= 0, "every configs/all.<type>.compack names a config kind");
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        if( kind >= 0 )
            check_names_in(catalog, path, (uint32_t)kind, "", &compacks);
    }
    if( dir )
        closedir(dir);

    snprintf(dir_path, sizeof(dir_path), "%s/interfaces", content);
    dir = opendir(dir_path);
    check(dir != NULL, "interfaces/ opens");
    while( dir && (entry = readdir(dir)) != NULL )
    {
        char path[2048];
        char prefix[256];
        if( !ends_with(entry->d_name, ".compack") )
            continue;
        snprintf(prefix, sizeof(prefix), "%.*s:", (int)(strlen(entry->d_name) - 8), entry->d_name);
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        check_names_in(catalog, path, TORIRSMAPED_CATALOG_COMPONENT, prefix, &ifaces);
    }
    if( dir )
        closedir(dir);

    printf("catalog: exact search found %ld/%ld names in %ld pack/*.pack, %ld/%ld in %ld "
           ".alloc, %ld/%ld in %ld config compacks, %ld/%ld components in %ld interfaces\n",
        packs.names - packs.missing, packs.names, packs.files, allocs.names - allocs.missing,
        allocs.names, allocs.files, compacks.names - compacks.missing, compacks.names,
        compacks.files, ifaces.names - ifaces.missing, ifaces.names, ifaces.files);
    snprintf(line, sizeof(line), "every name in pack/*.pack is found (%ld missing)", packs.missing);
    check(packs.missing == 0 && packs.names > 100000, line);
    snprintf(line, sizeof(line), "every name in pack/*.alloc is found (%ld missing)", allocs.missing);
    check(allocs.missing == 0 && allocs.names > 1000, line);
    snprintf(line, sizeof(line), "every compack name is found (%ld missing)", compacks.missing);
    check(compacks.missing == 0 && compacks.names > 100000, line);
    snprintf(line, sizeof(line), "every interface component is found (%ld missing)", ifaces.missing);
    check(ifaces.missing == 0 && ifaces.names > 10000, line);
}

static int
count_files(
    const char* dir_path,
    const char* suffix)
{
    DIR* dir = opendir(dir_path);
    struct dirent* entry;
    int count = 0;
    if( !dir )
        return 0;
    while( (entry = readdir(dir)) != NULL )
        count += ends_with(entry->d_name, suffix);
    closedir(dir);
    return count;
}

static int
first_record(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t flags)
{
    int record = -1;
    int count = ToriRSMapEdCatalog_Query(catalog, kind, filter, flags, &record, 1);
    return count > 0 ? record : -1;
}

static void
part_a(const char* content)
{
    struct ToriRSMapEdCatalog* catalog = ToriRSMapEdCatalog_Create(content);
    struct ToriRSMapEdCatalogStats stats;
    struct ToriRSMapEdCatalogSquare square;
    char path[1024];
    char value[256];
    int squares_on_disk;
    int record;
    int unmapped = 0;
    int records[8];
    int total;
    clock_t start;

    ToriRSMapEdCatalog_Refresh(catalog, 1, &stats);
    printf("catalog: index built in %.0f ms (scan %.0f, parse %.0f, merge %.0f): %d sources, "
           "%d records, %d squares\n",
        stats.scan_ms + stats.parse_ms + stats.merge_ms, stats.scan_ms, stats.parse_ms,
        stats.merge_ms, stats.sources, stats.records, stats.squares);
    check(stats.records > 300000, "the index holds the tree's records");

    ToriRSMapEdCatalog_Refresh(catalog, 1, &stats);
    printf("catalog: refresh with nothing changed: %.0f ms (scan %.0f), %d parsed, %d removed\n",
        stats.scan_ms + stats.parse_ms + stats.merge_ms, stats.scan_ms, stats.parsed, stats.removed);
    check(stats.parsed == 0 && stats.removed == 0, "an unchanged tree re-parses nothing");
    check(ToriRSMapEdCatalog_Refresh(catalog, 0, NULL) == 0, "a query inside the interval does not re-stat");
    /* 160,000 queries follow; none of them should re-stat the tree */
    ToriRSMapEdCatalog_SetRefreshInterval(catalog, 3600);

    check_every_pack_name(catalog, content);

    /* the atlas */
    snprintf(path, sizeof(path), "%s/maps", content);
    squares_on_disk = count_files(path, ".jm2");
    {
        DIR* dir;
        struct dirent* entry;
        snprintf(path, sizeof(path), "%s/ported", content);
        dir = opendir(path);
        while( dir && (entry = readdir(dir)) != NULL )
        {
            char lane[2048];
            if( entry->d_name[0] == '.' )
                continue;
            snprintf(lane, sizeof(lane), "%s/ported/%s/maps", content, entry->d_name);
            squares_on_disk += count_files(lane, ".jm2");
        }
        if( dir )
            closedir(dir);
    }
    printf("catalog: atlas %d squares, %d .jm2 on disk\n", ToriRSMapEdCatalog_SquareCount(catalog),
        squares_on_disk);
    check(ToriRSMapEdCatalog_SquareCount(catalog) == squares_on_disk,
        "the atlas has a row for every .jm2 in maps/ and every lane");

    check(ToriRSMapEdCatalog_Square(catalog, "m50_50", &square), "m50_50 is in the atlas");
    check(strcmp(square.area, "main") == 0, "m50_50 is claimed by the surface");
    check(square.rgb != 0, "m50_50 has a thumbnail colour");
    check((square.flags & TORIRSMAPED_SQUARE_HAS_LOCS) != 0, "m50_50 places locs");
    check(square.spawn_count > 0, "m50_50 has spawns");
    check(strcmp(square.storage, "maps") == 0, "m50_50 lives in maps/");
    printf("catalog: m50_50 area=%s rgb=%06X spawns=%d\n", square.area, square.rgb, square.spawn_count);

    check(ToriRSMapEdCatalog_Square(catalog, "m27_83", &square), "m27_83 is in the atlas");
    check(strcmp(square.area, "ancient_cavern") == 0, "m27_83 is the Ancient Cavern");
    printf("catalog: m27_83 area=%s (%d claiming)\n", square.area, square.area_count);

    record = -1;
    {
        DIR* dir;
        struct dirent* entry;
        snprintf(path, sizeof(path), "%s/maps", content);
        dir = opendir(path);
        while( dir && (entry = readdir(dir)) != NULL )
        {
            char key[64];
            if( !ends_with(entry->d_name, ".jm2") )
                continue;
            snprintf(key, sizeof(key), "%.*s", (int)(strlen(entry->d_name) - 4), entry->d_name);
            if( ToriRSMapEdCatalog_Square(catalog, key, &square) && strcmp(square.area, "none") == 0 )
                unmapped++;
        }
        if( dir )
            closedir(dir);
    }
    printf("catalog: %d squares claimed by no world-map area (the Unmapped tab)\n", unmapped);
    check(unmapped > 0 && unmapped < squares_on_disk / 2, "instance squares are unmapped, most are not");

    snprintf(path, sizeof(path), "ported/rs2012_qbd_td/maps/m16_101");
    if( ToriRSMapEdCatalog_Square(catalog, path, &square) )
        check(strcmp(square.storage, "ported/rs2012_qbd_td/maps") == 0, "a lane square keeps its storage");

    /* queries the panel makes */
    start = clock();
    total = ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_ANY, "door", 0, records, 8);
    printf("catalog: substring \"door\" over every kind: %d matches in %.1f ms\n", total,
        (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC);
    check(total > 100, "a substring search over every kind finds doors");
    total = ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_LOC, "op=Open", 0, records, 8);
    check(total > 100, "op=Open finds locs by any op slot");
    record = first_record(catalog, TORIRSMAPED_CATALOG_NPC, "Tool Leprechaun", TORIRSMAPED_CATALOG_EXACT);
    check(record >= 0, "an exact display name finds the npc");
    if( record >= 0 )
    {
        struct ToriRSMapEdCatalogEntry entry;
        ToriRSMapEdCatalog_Entry(catalog, record, &entry);
        check(strcmp(entry.name, "farming_tools_leprechaun") == 0, "the display name names the right npc");
        check(ToriRSMapEdCatalog_Field(catalog, record, "op1", value, sizeof(value))
                  && strcmp(value, "Talk-to") == 0,
            "the npc's op1 is summarised");
        check(ToriRSMapEdCatalog_Field(catalog, record, "dialogue", value, sizeof(value)),
            "an npc with an [opnpc1] script has dialogue");
    }
    total = ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_NPC, "dialogue=yes", 0, records, 8);
    check(total > 500, "dialogue=yes finds the npcs with a Talk-to script");
    total = ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_LOC, "multivar=varp0_mcannon", 0, records, 8);
    check(total > 0, "multivar= finds the multiloc shells a var drives");
    record = first_record(catalog, TORIRSMAPED_CATALOG_VARP, "varp0_mcannon", TORIRSMAPED_CATALOG_EXACT);
    check(record >= 0 && ToriRSMapEdCatalog_Field(catalog, record, "users", value, sizeof(value))
              && atoi(value) > 0,
        "a varp knows the multi shells that read it");
    total = ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_TRIGGER, "subject=farming_tools_leprechaun", 0, records, 8);
    check(total > 0, "trigger headers are found by subject");
    total = ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_SQUARE, "area=ancient_cavern", 0, records, 8);
    check(total > 0, "squares are found by area");
    total = ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_CS2, "kind=proc", 0, records, 8);
    check(total > 1000, "cs2 headers are found by kind");

    /* the record view over the real 32 MB all.npc, by its indexed offset */
    {
        struct ToriRSMapEdBuf request;
        struct ToriRSMapEdBuf answer;
        struct ToriRSMapEdReader reader;
        char text[512];
        char op1_source[512];
        uint32_t status;
        uint32_t count;
        int saw_overlay = 0;
        int saw_stats = 0;
        int op1_ok = 0;
        char paths[64][512];

        memset(&request, 0, sizeof(request));
        memset(&answer, 0, sizeof(answer));
        ToriRSMapEd_PutU32(&request, TORIRSMAPED_CATALOG_NPC);
        ToriRSMapEd_PutU32(&request, (uint32_t)-1);
        ToriRSMapEd_PutString(&request, "farming_tools_leprechaun");
        start = clock();
        check(ToriRSMapEdCatalog_HandleRecord(catalog, ToriRSMapEd_BufPeek(&request),
                  (uint32_t)ToriRSMapEd_BufAvailable(&request), &answer),
            "CATALOG_RECORD decodes its request");
        ToriRSMapEd_ReaderInit(&reader, ToriRSMapEd_BufPeek(&answer), (uint32_t)ToriRSMapEd_BufAvailable(&answer));
        status = ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetU32(&reader); /* kind */
        ToriRSMapEd_GetU32(&reader); /* id */
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        ToriRSMapEd_GetU32(&reader); /* provenance */
        count = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < count && i < 64; i++ )
        {
            uint32_t prov;
            ToriRSMapEd_GetString(&reader, paths[i], sizeof(paths[i]));
            prov = ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetU32(&reader);
            saw_overlay |= prov == TORIRSMAPED_PROV_OVERLAY
                           && strcmp(paths[i], "server/scripts/interface_farming/configs/farming_tools.npc") == 0;
            saw_stats |= prov == TORIRSMAPED_PROV_STATS;
        }
        count = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            char key[128];
            char value[4096];
            char old[4096];
            uint32_t source;
            ToriRSMapEd_GetString(&reader, key, sizeof(key));
            ToriRSMapEd_GetString(&reader, value, sizeof(value));
            source = ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetString(&reader, old, sizeof(old));
            ToriRSMapEd_GetU32(&reader);
            if( strcmp(key, "op1") == 0 && source < 64 )
            {
                snprintf(op1_source, sizeof(op1_source), "%s", paths[source]);
                op1_ok = strcmp(value, "Talk-to") == 0 && strcmp(op1_source, "configs/all.npc") == 0;
            }
        }
        printf("catalog: record farming_tools_leprechaun: %u fields in %.1f ms\n", count,
            (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC);
        check(status == EDITOR_HOST_OK && ToriRSMapEd_ReaderDone(&reader), "the record answers whole");
        check(op1_ok, "op1 is read back from all.npc at its offset, provenanced to it");
        check(saw_overlay, "the npc's server overlay is one of its sources");
        check(saw_stats, "and so is its npc_stats/npc_combat file");
        ToriRSMapEd_BufFree(&request);
        ToriRSMapEd_BufFree(&answer);
    }

    ToriRSMapEdCatalog_Free(catalog);
}

/* ------------------------------------------------------------------ */
/* Parts B and C                                                       */
/* ------------------------------------------------------------------ */

static const char* k_all_loc =
    "// test cache\n"
    "\n"
    "[test_door]\n"
    "name=Test door\n"
    "op1=Open\n"
    "op2=default\n"
    "category=cache_cat\n"
    "width=1\n"
    "\n"
    "[test_switch]\n"
    "name=Test switch\n"
    "multivarbit=varb9000_test_state\n"
    "multiloc1=test_door\n"
    "multiloc2=-1\n";

static const char* k_overlay_loc =
    "// a server overlay\n"
    "[test_door]\n"
    "category=overlay_cat\n"
    "extra_overlay_field=yes\n";

static int
build_scratch(
    const char* content,
    const char* root)
{
    char path[1024];
    char from[1024];
    static const char* const copies[] = {
        "configs/all.underlay", "configs/all.underlay.compack", "configs/all.overlay",
        "configs/all.overlay.compack", "maps/m50_50.jm2", "maps/m50_50.jl2",
        "worldmap/areas/details.wma", "worldmap/areas/compositemap.wmc", "pack/stat.pack",
        "server/scripts/areas/world/configs/m50_50.spawn",
    };
    static const char* const dirs[] = {
        "configs", "maps", "worldmap/areas", "pack", "server/scripts/areas/world/configs",
        "server/scripts/test_area/configs", "server/scripts/test_area/scripts", "ported/testlane",
    };
    int ok = 1;

    for( size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++ )
    {
        snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
        make_dirs(path);
    }
    for( size_t i = 0; i < sizeof(copies) / sizeof(copies[0]); i++ )
    {
        snprintf(from, sizeof(from), "%s/%s", content, copies[i]);
        snprintf(path, sizeof(path), "%s/%s", root, copies[i]);
        ok &= copy_file(from, path);
    }
#define WRITE(rel, text)                                                                           \
    do                                                                                             \
    {                                                                                              \
        snprintf(path, sizeof(path), "%s/%s", root, rel);                                          \
        ok &= write_text(path, text);                                                              \
    } while( 0 )
    WRITE("configs/all.loc", k_all_loc);
    WRITE("configs/all.loc.compack", "0=test_door\n1=test_switch\n");
    WRITE("configs/all.npc", "[test_guide]\nname=Guide\nop1=Talk-to\n");
    WRITE("configs/all.npc.compack", "0=test_guide\n");
    WRITE("configs/all.varbit", "[varb9000_test_state]\nbasevar=varp1_x\nstartbit=0\nendbit=1\n");
    WRITE("configs/all.varbit.compack", "9000=varb9000_test_state\n");
    WRITE("server/scripts/test_area/configs/test.loc", k_overlay_loc);
    WRITE("server/scripts/test_area/scripts/test.rs2",
        "[opnpc1,test_guide]\n~chatnpc(\"hi\");\n\n[oploc1,test_door]\n~open();\n");
#undef WRITE
    return ok;
}

static const struct EditorHost_CatalogField*
row_field(
    const struct EditorHost_CatalogRow* row,
    const char* key)
{
    for( int i = 0; i < row->field_count; i++ )
    {
        if( strcmp(row->fields[i].key, key) == 0 )
            return &row->fields[i];
    }
    return NULL;
}

static const struct EditorHost_CatalogRecordField*
record_field(
    const struct EditorHost_CatalogRecord* record,
    const char* key)
{
    for( int i = 0; i < record->field_count; i++ )
    {
        if( strcmp(record->fields[i].key, key) == 0 )
            return &record->fields[i];
    }
    return NULL;
}

static const struct EditorHost_CatalogSquare*
atlas_square(
    const struct EditorHost_CatalogSquares* atlas,
    int x,
    int z,
    const char* storage)
{
    for( int i = 0; i < atlas->square_count; i++ )
    {
        const struct EditorHost_CatalogSquare* sq = &atlas->squares[i];
        if( sq->map_x == x && sq->map_z == z && strcmp(atlas->storages[sq->storage], storage) == 0 )
            return sq;
    }
    return NULL;
}

static void
part_b(const char* root)
{
    struct EditorHost host;
    struct EditorHost_CatalogPage page;
    struct EditorHost_CatalogRecord record;
    struct EditorHost_CatalogSquares atlas;
    struct EditorHostMapEdFacts sink;
    struct EditorHost_DocCreate create;
    const struct EditorHost_CatalogSquare* sq;
    char first_name[128];

    memset(&host, 0, sizeof(host));
    memset(&sink, 0, sizeof(sink));
    if( !Editor_HostOpenMapEdEmbed(&host, root, NULL) )
    {
        check(0, "the embedded server opens the scratch tree");
        return;
    }
    check((Editor_HostMapEdCapabilities(&host) & TORIRSMAPED_CAP_CATALOG) != 0,
        "FACT_HELLO advertises the catalog");

    /* paging */
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_LOC, "", 0, 0, 1, &page) == EDITOR_HOST_OK,
        "CATALOG_QUERY answers");
    check(page.total == 2 && page.row_count == 1, "page 0 of size 1 holds one of the two locs");
    snprintf(first_name, sizeof(first_name), "%s", page.row_count ? page.rows[0].name : "");
    Editor_HostCatalogPageFree(&page);
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_LOC, "", 0, 1, 1, &page) == EDITOR_HOST_OK
              && page.row_count == 1 && strcmp(page.rows[0].name, first_name) != 0,
        "page 1 holds the other");
    Editor_HostCatalogPageFree(&page);
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_LOC, "", 0, 2, 1, &page) == EDITOR_HOST_OK
              && page.row_count == 0 && page.total == 2,
        "page 2 is past the end");
    Editor_HostCatalogPageFree(&page);
    check(Editor_HostMapEdCatalogQuery(&host, 9999, "", 0, 0, 0, &page) == EDITOR_HOST_REFUSED,
        "an unknown kind is refused");
    Editor_HostCatalogPageFree(&page);

    /* exact, filters, overlay-over-cache summary */
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_LOC, "test_door",
              TORIRSMAPED_CATALOG_EXACT, 0, 0, &page) == EDITOR_HOST_OK && page.row_count == 1,
        "exact search finds test_door");
    if( page.row_count == 1 )
    {
        const struct EditorHost_CatalogField* field = row_field(&page.rows[0], "category");
        check(page.rows[0].id == 0, "test_door has its compack id");
        check(strcmp(page.rows[0].display, "Test door") == 0, "display is the name= field");
        check(field && strcmp(field->value, "overlay_cat") == 0, "the overlay's category wins the summary");
        check(row_field(&page.rows[0], "op2") == NULL, "default fields are not summarised");
        check((page.rows[0].provenance & (TORIRSMAPED_PROV_CACHE | TORIRSMAPED_PROV_OVERLAY
                                             | TORIRSMAPED_PROV_COMPACK))
                  == (TORIRSMAPED_PROV_CACHE | TORIRSMAPED_PROV_OVERLAY | TORIRSMAPED_PROV_COMPACK),
            "provenance names cache, overlay and compack");
    }
    Editor_HostCatalogPageFree(&page);
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_ANY, "multivar=varb9000", 0, 0, 0, &page)
                  == EDITOR_HOST_OK
              && page.row_count == 1 && strcmp(page.rows[0].name, "test_switch") == 0,
        "multivar=varb9000 names test_switch's varbit");
    if( page.row_count == 1 )
    {
        const struct EditorHost_CatalogField* slots = row_field(&page.rows[0], "slots");
        check(slots && strcmp(slots->value, "2") == 0, "a multiloc shell counts its slots");
    }
    Editor_HostCatalogPageFree(&page);
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_LOC, "op=Open test", 0, 0, 0, &page)
                  == EDITOR_HOST_OK
              && page.row_count == 1 && strcmp(page.rows[0].name, "test_door") == 0,
        "op=Open plus a free term");
    Editor_HostCatalogPageFree(&page);
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_NPC, "dialogue=yes", 0, 0, 0, &page)
                  == EDITOR_HOST_OK
              && page.row_count == 1 && strcmp(page.rows[0].name, "test_guide") == 0,
        "an [opnpc1] script gives the npc dialogue=yes");
    Editor_HostCatalogPageFree(&page);
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_VARBIT, "9000", TORIRSMAPED_CATALOG_EXACT,
              0, 0, &page) == EDITOR_HOST_OK
              && page.row_count == 1,
        "exact search by id");
    if( page.row_count == 1 )
    {
        const struct EditorHost_CatalogField* users = row_field(&page.rows[0], "users");
        check(users && strcmp(users->value, "1") == 0, "the varbit counts its one multi shell");
    }
    Editor_HostCatalogPageFree(&page);
    check(Editor_HostMapEdCatalogQuery(&host, TORIRSMAPED_CATALOG_TRIGGER, "oploc1,test_door",
              TORIRSMAPED_CATALOG_EXACT, 0, 0, &page) == EDITOR_HOST_OK
              && page.row_count == 1,
        "a trigger header is a record");
    if( page.row_count == 1 )
    {
        const struct EditorHost_CatalogField* line = row_field(&page.rows[0], "line");
        check(line && strcmp(line->value, "4") == 0, "the trigger knows its line");
    }
    Editor_HostCatalogPageFree(&page);

    /* the record view */
    check(Editor_HostMapEdCatalogRecord(&host, TORIRSMAPED_CATALOG_LOC, -1, "test_door", &record)
              == EDITOR_HOST_OK,
        "CATALOG_RECORD answers by name");
    {
        const struct EditorHost_CatalogRecordField* category = record_field(&record, "category");
        const struct EditorHost_CatalogRecordField* op1 = record_field(&record, "op1");
        const struct EditorHost_CatalogRecordField* op2 = record_field(&record, "op2");
        const struct EditorHost_CatalogRecordField* extra = record_field(&record, "extra_overlay_field");
        check(record.source_count == 3, "three sources: compack, the cache block, the overlay");
        check(category && strcmp(category->value, "overlay_cat") == 0, "the overlay's value is the record's");
        check(category && category->source >= 0
                  && strcmp(record.sources[category->source].path, "server/scripts/test_area/configs/test.loc") == 0
                  && record.sources[category->source].provenance == TORIRSMAPED_PROV_OVERLAY,
            "category is provenanced to the overlay file");
        check(category && category->overridden_value && strcmp(category->overridden_value, "cache_cat") == 0
                  && category->overridden_source >= 0
                  && strcmp(record.sources[category->overridden_source].path, "configs/all.loc") == 0,
            "and carries the cache value it overrides");
        check(op1 && op1->source >= 0 && strcmp(record.sources[op1->source].path, "configs/all.loc") == 0
                  && !op1->overridden_value,
            "op1 comes from the cache block, overriding nothing");
        check(op2 && strcmp(op2->value, "default") == 0, "the record view keeps default fields");
        check(extra && strcmp(record.sources[extra->source].path, "server/scripts/test_area/configs/test.loc") == 0,
            "an overlay-only field is the overlay's");
        check(category && record.sources[category->source].line == 2, "the overlay block's line");
    }
    Editor_HostCatalogRecordFree(&record);
    check(Editor_HostMapEdCatalogRecord(&host, TORIRSMAPED_CATALOG_LOC, 1, "", &record) == EDITOR_HOST_OK
              && strcmp(record.name, "test_switch") == 0,
        "CATALOG_RECORD answers by id");
    Editor_HostCatalogRecordFree(&record);
    check(Editor_HostMapEdCatalogRecord(&host, TORIRSMAPED_CATALOG_LOC, -1, "no_such_loc", &record)
              == EDITOR_HOST_ABSENT,
        "an unknown record is ABSENT");
    Editor_HostCatalogRecordFree(&record);

    /* the atlas */
    check(Editor_HostMapEdCatalogSquares(&host, &atlas) == EDITOR_HOST_OK, "CATALOG_SQUARES answers");
    check(atlas.storage_count == 2 && strcmp(atlas.storages[0], "maps") == 0
              && strcmp(atlas.storages[1], "ported/testlane/maps") == 0,
        "storages: maps/, and the lane directory that has no squares yet");
    check(atlas.area_count == 52, "the 52 world-map areas");
    check(atlas.square_count == 1, "one square on disk");
    sq = atlas_square(&atlas, 50, 50, "maps");
    check(sq != NULL, "m50_50's row");
    if( sq )
    {
        check(sq->primary_area >= 0 && strcmp(atlas.areas[sq->primary_area].internal_name, "main") == 0
                  && atlas.areas[sq->primary_area].is_main,
            "m50_50 is on the surface");
        check(sq->rgb != 0, "with a colour");
        check(sq->flags
                  == (TORIRSMAPED_SQUARE_HAS_JM2 | TORIRSMAPED_SQUARE_HAS_JL2 | TORIRSMAPED_SQUARE_HAS_LOCS),
            "jm2, jl2, locs; not open");
        check(sq->spawn_count > 0, "and its spawns");
    }
    Editor_HostCatalogSquaresFree(&atlas);

    /* DOC_CREATE: the new row reaches every window before anything is on disk */
    check(Editor_HostMapEdCatalogSquaresChanged(&host, &atlas) == 0, "no atlas broadcast yet");
    memset(&create, 0, sizeof(create));
    create.kind = EDITOR_DOC_SQUARE;
    snprintf(create.key, sizeof(create.key), "m51_50");
    snprintf(create.storage, sizeof(create.storage), "ported/testlane/maps");
    create.template_kind = EDITOR_DOC_TEMPLATE_EMPTY;
    check(Editor_HostMapEdDocCreate(&host, &create) == EDITOR_HOST_OK, "DOC_CREATE in the lane");
    Editor_HostMapEdDrainFacts(&host, &sink);
    check(Editor_HostMapEdCatalogSquaresChanged(&host, &atlas) == 1, "DOC_CREATE broadcasts the atlas");
    sq = atlas_square(&atlas, 51, 50, "ported/testlane/maps");
    check(sq && (sq->flags & TORIRSMAPED_SQUARE_CREATED) && (sq->flags & TORIRSMAPED_SQUARE_OPEN)
              && !(sq->flags & TORIRSMAPED_SQUARE_HAS_JM2),
        "the created square's row: open, created, nothing on disk");
    check(sq && sq->primary_area >= 0 && strcmp(atlas.areas[sq->primary_area].internal_name, "main") == 0,
        "and already claimed by its area");
    Editor_HostCatalogSquaresFree(&atlas);

    check(Editor_HostMapEdSave(&host, EDITOR_DOC_SQUARE, "m51_50") == 1, "Save writes the new square");
    Editor_HostMapEdDrainFacts(&host, &sink);
    check(Editor_HostMapEdCatalogSquaresChanged(&host, &atlas) == 1, "the save broadcasts the atlas");
    sq = atlas_square(&atlas, 51, 50, "ported/testlane/maps");
    check(sq && !(sq->flags & TORIRSMAPED_SQUARE_CREATED) && (sq->flags & TORIRSMAPED_SQUARE_HAS_JM2)
              && (sq->flags & TORIRSMAPED_SQUARE_OPEN),
        "now on disk, still open, no longer 'created'");
    check(atlas.square_count == 2, "two squares on disk");
    Editor_HostCatalogSquaresFree(&atlas);

    Editor_HostClose(&host);
}

static void
part_c(const char* root)
{
    struct ToriRSMapEdCatalog* catalog = ToriRSMapEdCatalog_Create(root);
    struct ToriRSMapEdCatalogStats stats;
    struct ToriRSMapEdCatalogSquare square;
    char path[1024];
    char from[1024];
    char value[64];
    int record;

    ToriRSMapEdCatalog_Refresh(catalog, 1, &stats);
    check(stats.records > 0, "the scratch tree indexes");

    snprintf(path, sizeof(path), "%s/server/scripts/test_area/configs/test.loc", root);
    write_text(path, "[test_door]\ncategory=edited_category\n");
    ToriRSMapEdCatalog_Refresh(catalog, 1, &stats);
    check(stats.parsed == 1, "an edited overlay is the only file re-parsed");
    record = first_record(catalog, TORIRSMAPED_CATALOG_LOC, "test_door", TORIRSMAPED_CATALOG_EXACT);
    check(record >= 0 && ToriRSMapEdCatalog_Field(catalog, record, "category", value, sizeof(value))
              && strcmp(value, "edited_category") == 0,
        "the edit is in the index");

    snprintf(from, sizeof(from), "%s/maps/m50_50.jm2", root);
    snprintf(path, sizeof(path), "%s/maps/m52_50.jm2", root);
    copy_file(from, path);
    ToriRSMapEdCatalog_Refresh(catalog, 1, &stats);
    check(stats.parsed == 1, "a new square is parsed alone");
    check(ToriRSMapEdCatalog_Square(catalog, "m52_50", &square) && square.rgb != 0,
        "and appears in the atlas, coloured");
    remove(path);
    ToriRSMapEdCatalog_RefreshMaps(catalog);
    check(!ToriRSMapEdCatalog_Square(catalog, "m52_50", &square), "a deleted square leaves the atlas");

    /* A new, empty lane that sorts before the one holding a square: no file
     * changed, but every row's storage index moved. */
    snprintf(path, sizeof(path), "%s/ported/alane", root);
    make_dirs(path);
    ToriRSMapEdCatalog_RefreshMaps(catalog);
    check(ToriRSMapEdCatalog_Square(catalog, "ported/testlane/maps/m51_50", &square)
              && strcmp(square.storage, "ported/testlane/maps") == 0,
        "a new lane directory does not shift another lane's squares");

    ToriRSMapEdCatalog_Free(catalog);
}

int
main(
    int argc,
    char** argv)
{
    char root[1024];

    if( argc != 3 )
    {
        fprintf(stderr, "usage: catalog_test <content-dir> <scratch-dir>\n");
        return 2;
    }

    part_a(argv[1]);

    snprintf(root, sizeof(root), "%s/content", argv[2]);
    check(build_scratch(argv[1], root), "the scratch tree is written");
    part_b(root);
    part_c(root);

    printf("catalog_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
