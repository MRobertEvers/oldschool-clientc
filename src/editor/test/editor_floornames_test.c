/*
 * What a mapper calls a floor record (editor/editor_floornames.c), content-free.
 *
 *   common     the set is underlays then overlays, no id twice in a kind,
 *              every name fits and is lower case; an entry answers only for
 *              the record it describes (the colour, or the texture); one
 *              floor each for a dirt track, a pavement and water.
 *   words      a colour in a person's words: the grass is green, the track
 *              brown, sand tan, a grey stone, the client's magenta key says
 *              what it is.
 *   names      the common name first, then the texture's, then the colour's.
 *   search     "dirt", "water", "wood", an id, a "#3d2b" prefix; every word
 *              must match; a word that means another finds it.
 *
 * With the content tree's configs/ directory as its argument it also checks
 * the set against that tree: every common entry's record is there and is
 * what the entry says (a record renumbered or recoloured upstream fails
 * here, not as a wrong name in the Palette).
 */

#include "editor/editor_floornames.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                                     \
    do                                                                                                       \
    {                                                                                                        \
        g_checks++;                                                                                          \
        if( !(cond) )                                                                                        \
        {                                                                                                    \
            g_failed++;                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                      \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
        }                                                                                                    \
    } while( 0 )

static void
test_common(void)
{
    int count = 0;
    const struct Editor_FloorCommon* list = Editor_FloorCommonList(&count);
    int roles[4] = { 0, 0, 0, 0 };
    int ordered = 1;
    int unique = 1;
    int named = 1;
    int underlays = 0;

    CHECK(list && count >= 30, "common: a set of at least thirty floors (%d)", count);
    for( int i = 0; i < count; i++ )
    {
        size_t const length = strlen(list[i].name);

        if( !list[i].is_overlay )
            underlays++;
        if( i > 0 && list[i - 1].is_overlay && !list[i].is_overlay )
            ordered = 0;
        for( int j = 0; j < i; j++ )
            if( list[j].is_overlay == list[i].is_overlay && list[j].id == list[i].id )
                unique = 0;
        if( length == 0 || length >= EDITOR_FLOOR_NAME_MAX )
            named = 0;
        for( size_t c = 0; c < length; c++ )
            if( list[i].name[c] >= 'A' && list[i].name[c] <= 'Z' )
                named = 0;
        if( list[i].id <= 0 || (!list[i].is_overlay && list[i].texture >= 0) )
            named = 0;
        roles[list[i].role]++;
    }
    CHECK(ordered && underlays >= 12 && count - underlays >= 12,
        "common: underlays then overlays, a dozen or more of each (%d and %d)", underlays, count - underlays);
    CHECK(unique, "common: no id twice in a kind");
    CHECK(named, "common: every name is lower case and fits; only an overlay lays a texture");
    CHECK(roles[EDITOR_FLOOR_ROLE_DIRT_TRACK] == 1 && roles[EDITOR_FLOOR_ROLE_PAVING] == 1
              && roles[EDITOR_FLOOR_ROLE_WATER] == 1,
        "common: one dirt track, one pavement, one water (%d %d %d)", roles[EDITOR_FLOOR_ROLE_DIRT_TRACK],
        roles[EDITOR_FLOOR_ROLE_PAVING], roles[EDITOR_FLOOR_ROLE_WATER]);

    /* An entry answers for the record it describes and no other. */
    CHECK(Editor_FloorCommonFind(0, 48, 0x35720A, -1) && strcmp(Editor_FloorCommonFind(0, 48, 0x35720A, -1)->name, "grass") == 0,
        "common: underlay 48 in #35720a is the grass");
    CHECK(!Editor_FloorCommonFind(0, 48, 0x40a070, -1), "common: underlay 48 in another colour is not");
    CHECK(!Editor_FloorCommonFind(1, 48, 0x35720A, -1), "common: nor is overlay 48");
    CHECK(Editor_FloorCommonFind(1, 6, 0x123456, 1) && Editor_FloorCommonFind(1, 6, 0x123456, 1)->role == EDITOR_FLOOR_ROLE_WATER,
        "common: overlay 6 laying texture 1 is the water, whatever its colour");
    CHECK(!Editor_FloorCommonFind(1, 6, 0x123456, 3) && !Editor_FloorCommonFind(1, 6, 0x123456, -1),
        "common: overlay 6 laying another texture, or none, is not");
    CHECK(Editor_FloorCommonFind(1, 6, 0, -2) != NULL, "common: a textured entry is trusted until its texture is known");
    CHECK(Editor_FloorCommonFind(1, 14, 0x3D2B0B, -2) && !Editor_FloorCommonFind(1, 14, 0x3D2B0C, -2),
        "common: a plain entry is checked on its colour even then");
    CHECK(!Editor_FloorCommonFind(1, 14, 0x3D2B0B, 7), "common: a plain entry whose record lays a texture is not it");
}

static void
test_words(void)
{
    static const struct
    {
        uint32_t rgb;
        const char* want;
    } k_cases[] = {
        { 0x35720A, "dark green (grass)" }, { 0x3D2B0B, "dark brown (dirt)" }, { 0xCBBA76, "light tan (sand)" },
        { 0x4D4D4D, "grey (stone)" },       { 0x030303, "black" },             { 0xEEEEEE, "white (snow)" },
        { 0xFF00FF, "magenta (drawn as no overlay)" }, { 0x78680B, "dark olive (dry grass)" },
        { 0x406D9C, "blue (water)" },
    };
    char name[EDITOR_FLOOR_NAME_MAX];

    for( size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++ )
    {
        Editor_FloorColourWords(k_cases[i].rgb, name, sizeof(name));
        CHECK(strcmp(name, k_cases[i].want) == 0, "words: #%06x is \"%s\" (got \"%s\")", (unsigned)k_cases[i].rgb,
            k_cases[i].want, name);
    }
    /* Every colour has words that fit. */
    {
        int fits = 1;
        for( uint32_t rgb = 0; rgb <= 0xffffff; rgb += 0x030507 )
        {
            memset(name, 0x7f, sizeof(name));
            Editor_FloorColourWords(rgb, name, sizeof(name));
            if( !memchr(name, '\0', sizeof(name)) || name[0] == '\0' )
                fits = 0;
        }
        CHECK(fits, "words: every colour gets a name that fits");
    }
}

static void
test_names(void)
{
    char name[EDITOR_FLOOR_NAME_MAX];

    Editor_FloorName(1, 14, 0x3D2B0B, -1, name, sizeof(name));
    CHECK(strcmp(name, "dirt track") == 0, "names: overlay 14 is the dirt track (%s)", name);
    Editor_FloorName(1, 35, 0, 3, name, sizeof(name));
    CHECK(strcmp(name, "wood planks") == 0, "names: another overlay laying texture 3 is wood planks (%s)", name);
    Editor_FloorName(1, 900, 0, 77, name, sizeof(name));
    CHECK(strcmp(name, "texture 77") == 0, "names: a texture with no name says its number (%s)", name);
    Editor_FloorName(0, 200, 0x35720A, -1, name, sizeof(name));
    CHECK(strcmp(name, "dark green (grass)") == 0, "names: any other record is its colour's words (%s)", name);
    CHECK(Editor_FloorTextureName(1) && strcmp(Editor_FloorTextureName(1), "water") == 0
              && Editor_FloorTextureName(140) && !Editor_FloorTextureName(77),
        "names: texture 1 is water, 140 a tinted water, 77 has none");
}

static void
test_search(void)
{
    CHECK(Editor_FloorMatches("", 5, "anything", 0), "search: an empty query finds everything");
    CHECK(Editor_FloorMatches("dirt", 14, "dirt track", 0x3D2B0B) && Editor_FloorMatches("DIRT", 14, "dirt track", 0),
        "search: a word of the name, letter case aside");
    CHECK(Editor_FloorMatches("water", 6, "water", 0) && Editor_FloorMatches("wood", 5, "wood floor (planks)", 0)
              && !Editor_FloorMatches("wood", 6, "water", 0),
        "search: water finds water, wood the plank floor, wood not water");
    CHECK(Editor_FloorMatches("road", 14, "dirt track", 0) && Editor_FloorMatches("mud", 64, "earth (dirt)", 0)
              && Editor_FloorMatches("rock", 10, "stone floor", 0) && Editor_FloorMatches("planks", 52, "polished wood floor", 0)
              && Editor_FloorMatches("pond", 6, "water", 0),
        "search: a word that means one in the name finds it");
    CHECK(Editor_FloorMatches("14", 14, "dirt track", 0) && !Editor_FloorMatches("14", 141, "x", 0)
              && !Editor_FloorMatches("4", 14, "dirt track", 0),
        "search: a number is the whole id");
    CHECK(Editor_FloorMatches("#3d2b", 14, "dirt track", 0x3D2B0B) && Editor_FloorMatches("#3D2B0B", 14, "x", 0x3D2B0B)
              && !Editor_FloorMatches("#3d2c", 14, "dirt track", 0x3D2B0B) && !Editor_FloorMatches("#3d2b0b0", 14, "x", 0x3D2B0B),
        "search: # and the first digits of the colour");
    CHECK(Editor_FloorMatches("dark  stone ", 2, "dark stone floor", 0) && !Editor_FloorMatches("dark wood", 2, "dark stone floor", 0),
        "search: every word must match");
}

/** `key=0xRRGGBB` or `key=<number>` of the block `[<type>_<config id>]` in
 *  `text`: 1 and the value; 0 when the block or the key is missing or says
 *  "default". */
static int
block_value(
    const char* text,
    const char* type,
    int config_id,
    const char* key,
    long* out)
{
    char header[64];
    const char* at;
    const char* end;
    size_t const key_length = strlen(key);

    snprintf(header, sizeof(header), "[%s_%d]", type, config_id);
    at = strstr(text, header);
    if( !at )
        return 0;
    at += strlen(header);
    end = strstr(at, "\n[");
    while( at && (!end || at < end) )
    {
        at = strchr(at, '\n');
        if( !at || (end && at >= end) )
            return 0;
        at++;
        if( strncmp(at, key, key_length) == 0 && at[key_length] == '=' )
        {
            char* stop = NULL;
            long const value = strtol(at + key_length + 1, &stop, 0);
            if( stop == at + key_length + 1 )
                return 0;
            *out = value;
            return 1;
        }
    }
    return 0;
}

static char*
read_file(const char* path)
{
    FILE* file = fopen(path, "rb");
    char* data;
    long size;

    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = malloc((size_t)size + 1);
    assert(data);
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        fclose(file);
        free(data);
        return NULL;
    }
    fclose(file);
    data[size] = '\0';
    return data;
}

static void
test_tree(const char* configs_dir)
{
    char path[1024];
    char* texts[2];
    int count = 0;
    const struct Editor_FloorCommon* list = Editor_FloorCommonList(&count);

    snprintf(path, sizeof(path), "%s/all.underlay", configs_dir);
    texts[0] = read_file(path);
    snprintf(path, sizeof(path), "%s/all.overlay", configs_dir);
    texts[1] = read_file(path);
    CHECK(texts[0] && texts[1], "tree: %s holds all.underlay and all.overlay", configs_dir);
    if( texts[0] && texts[1] )
        for( int i = 0; i < count; i++ )
        {
            const char* const type = list[i].is_overlay ? "overlay" : "underlay";
            long value = -1;

            if( list[i].texture >= 0 )
                CHECK(block_value(texts[1], type, list[i].id - 1, "texture", &value) && value == list[i].texture,
                    "tree: overlay %d (%s) lays texture %d (the tree says %ld)", list[i].id, list[i].name,
                    list[i].texture, value);
            else
                CHECK(block_value(texts[list[i].is_overlay], type, list[i].id - 1, "colour", &value)
                          && (uint32_t)value == list[i].rgb
                          && (!list[i].is_overlay || !block_value(texts[1], type, list[i].id - 1, "texture", &value)),
                    "tree: %s %d (%s) is #%06x and lays no texture (the tree says #%06lx)", type, list[i].id,
                    list[i].name, (unsigned)list[i].rgb, value);
        }
    free(texts[0]);
    free(texts[1]);
}

int
main(
    int argc,
    char** argv)
{
    test_common();
    test_words();
    test_names();
    test_search();
    if( argc > 1 )
        test_tree(argv[1]);
    printf("editor_floornames_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
