/*
 * The Chambers of Xeric helper, without a client.
 *
 *   make -C src test-nxt-cox-helper
 *
 * Three things, each against its own authority:
 *
 *   - the room table against the server's template table (cox_layout.rs2);
 *   - the room WALK against the server's own generator, re-stated here step
 *     for step (~cox_layout_cell_x / _exit_dir / _variant / _rotation), so a
 *     layout the server can build is one this client lists in order;
 *   - the PAINT against the C++ client's one published picture
 *     (plugin/test/data/cox_helper_wiki.png, the wiki's
 *     Chambers_of_Xeric_helper.png), pixel for pixel: the same eleven rooms
 *     painted from the shipped art and laid over black, as the picture is.
 */
#include "stb_image.h"

#include "plugin/plugins/nxt_cox_helper.h"
#include "plugin/plugins/nxt_cox_layout.h"
#include "plugin/plugins/plugin_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define ASSETS "../script/plugins/assets/nxt-cox-helper/"
#define WIKI "plugin/test/data/cox_helper_wiki.png"

/* ---- the server's generator, restated ------------------------------- */

static int
gen_cell_x(int step, int seed)
{
    int const row = step / 4;
    int const col = step % 4;
    return ((row + seed) % 2 == 1) ? 3 - col : col;
}

static int
gen_exit_dir(int step, int seed)
{
    int x0;
    int x1;
    if( step >= 7 )
        return 0;
    x0 = gen_cell_x(step, seed);
    x1 = gen_cell_x(step + 1, seed);
    if( x1 > x0 )
        return 1;
    if( x1 < x0 )
        return 3;
    return (step + 1) / 4 > step / 4 ? 0 : 2;
}

static int
gen_entry_dir(int step, int seed)
{
    return step == 0 ? gen_exit_dir(0, seed) : gen_exit_dir(step - 1, seed);
}

static int
gen_variant(int step, int seed)
{
    int const from = gen_entry_dir(step, seed);
    int const to = gen_exit_dir(step, seed);
    if( from == to )
        return 1;
    if( (from + 1) % 4 == to )
        return 2;
    return 0;
}

/* Eight rooms a floor, one of each row of the table, by template. */
static int const FLOOR_ZCELL[8] = { 165, 164, 167, 163, 166, 168, 165, 170 };
static int const FLOOR_PLANE[8] = { 1, 2, 1, 0, 1, 0, 0, 0 };

static void
gen_floor(struct NxtCoxLayout* layout, int level, int seed, int base_x, int base_z, int const* order)
{
    for( int k = 0; k < 8; k++ )
    {
        int const step = order[k];
        int const variant = gen_variant(step, seed);
        int const kind =
            NxtCox_RoomKindOf(3264 + variant * 32, FLOOR_ZCELL[step] * 32, FLOOR_PLANE[step]);
        CHECK(kind >= 0, "every generated room is in the table");
        if( kind < 0 )
            continue;
        NxtCoxLayout_Note(
            layout, level, base_x + gen_cell_x(step, seed), base_z + step / 4, kind, variant,
            gen_entry_dir(step, seed));
    }
}

static void
test_table(void)
{
    struct
    {
        int zcell, plane;
        char const* name;
    } const rows[] = {
        { 163, 0, "Scavenger" }, { 163, 1, "Scavenger" }, { 164, 0, "Shamans" },
        { 164, 1, "Mystics" },   { 164, 2, "Guardians" }, { 165, 0, "Vasa" },
        { 165, 1, "Tekton" },    { 165, 2, "Vespula" },   { 166, 0, "Vanguards" },
        { 166, 1, "Muttadile" }, { 167, 0, "Ice demon" }, { 167, 1, "Tightrope" },
        { 167, 2, "Crabs" },     { 168, 0, "Thieving" },  { 170, 0, "Fish" },
        { 170, 1, "Bats" },
    };
    for( size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++ )
    {
        for( int v = 0; v < 3; v++ )
        {
            /* Any zone of the room names it, not only its corner. */
            int const kind = NxtCox_RoomKindOf(3264 + v * 32 + 24, rows[i].zcell * 32 + 8, rows[i].plane);
            CHECK(kind >= 0 && strcmp(NxtCox_RoomKind(kind)->name, rows[i].name) == 0, rows[i].name);
        }
    }
    CHECK(NxtCox_RoomKindOf(3200, 178 * 32, 0) < 0, "the Olm chamber (x 100) is no listed room");
    CHECK(NxtCox_RoomKindOf(3264, 5248, 3) < 0, "an unauthored plane is no room");
    CHECK(NxtCox_RoomVariantOf(3264) == 0 && NxtCox_RoomVariantOf(3328 + 31) == 2, "variants by x");
    CHECK(NxtCox_RoomVariantOf(3360) < 0, "x cell 105 is no variant");
}

static void
test_walk(void)
{
    /* Seen in a scrambled order, as zones of a scene arrive. */
    static int const SCRAMBLE[8] = { 5, 2, 7, 0, 3, 6, 1, 4 };
    struct NxtCoxLayout layout;
    int out[32];
    int n;

    for( int seed = 0; seed < 2; seed++ )
    {
        NxtCoxLayout_Reset(&layout);
        gen_floor(&layout, 1, seed + 1, 40, 80, SCRAMBLE);
        gen_floor(&layout, 0, seed, 40, 80, SCRAMBLE);
        NxtCoxLayout_Visit(&layout, 0);
        n = NxtCoxLayout_Order(&layout, out, 32);
        CHECK(n == 16, "both floors listed");
        for( int k = 0; k < n && k < 16; k++ )
        {
            int const step = k % 8;
            int const level = k / 8;
            int const s = level == 0 ? seed : seed + 1;
            struct NxtCoxCell const* c = &layout.cells[out[k]];
            CHECK(c->level == level, "the floor stood on first, then the other");
            CHECK(c->cell_x == 40 + gen_cell_x(step, s) && c->cell_z == 80 + step / 4,
                "rooms in the server's path order");
        }
    }

    /* A floor seen in part: what was seen, in order, nothing invented. */
    NxtCoxLayout_Reset(&layout);
    {
        static int const FIRST_THREE[8] = { 2, 0, 1, 0, 0, 0, 0, 0 };
        gen_floor(&layout, 0, 0, 40, 80, FIRST_THREE);
    }
    n = NxtCoxLayout_Order(&layout, out, 32);
    CHECK(n == 3, "three seen, three listed");
    for( int k = 0; k < n; k++ )
        CHECK(layout.cells[out[k]].cell_x == 40 + k, "the seen part in order");
}

/*
 * A reroll goes from one raid's start straight into the next's, with no scene
 * between that has no raid room (RSProx 4392: six raids before the one
 * played). The table is one raid's, so a start anywhere else is another raid.
 */
static void
test_reroll(void)
{
    static int const ALL[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    int const start = NxtCox_RoomKindOf(3264, 162 * 32, 3);
    struct NxtCoxLayout layout;

    CHECK(start >= 0 && NxtCox_RoomIsRaidStart(start), "row 162 is the raid's start");
    CHECK(!NxtCox_RoomIsRaidStart(NxtCox_RoomKindOf(3264, 178 * 32, 3)), "a floor's start is not");

    NxtCoxLayout_Reset(&layout);
    CHECK(!NxtCoxLayout_IsAnotherRaid(&layout, 3, 430, 82), "nothing remembered, nothing to forget");
    NxtCoxLayout_Note(&layout, 3, 430, 82, start, 0, 0);
    gen_floor(&layout, 3, 0, 430, 80, ALL);
    CHECK(!NxtCoxLayout_IsAnotherRaid(&layout, 3, 430, 82), "the same start is the same raid");
    CHECK(NxtCoxLayout_IsAnotherRaid(&layout, 3, 340, 88), "a start elsewhere is another raid");
    CHECK(NxtCoxLayout_IsAnotherRaid(&layout, 2, 430, 82), "a start on another level too");
}

static void
test_format(void)
{
    char s[32];

    NxtCoxHelper_FormatScore(s, sizeof(s), 0);
    CHECK(strcmp(s, "0") == 0, "0");
    NxtCoxHelper_FormatScore(s, sizeof(s), 1234567);
    CHECK(strcmp(s, "1,234,567") == 0, "comma thousands");
    NxtCoxHelper_FormatScore(s, sizeof(s), 999);
    CHECK(strcmp(s, "999") == 0, "no comma under a thousand");
    NxtCoxHelper_FormatScore(s, sizeof(s), 2000000000);
    CHECK(strcmp(s, "Lots!") == 0, "Lots! from two billion");
    NxtCoxHelper_FormatTime(s, sizeof(s), 0);
    CHECK(strcmp(s, "00:00:00") == 0, "the picture's zero");
    NxtCoxHelper_FormatTime(s, sizeof(s), 100);
    CHECK(strcmp(s, "00:01:00") == 0, "100 ticks is a minute");
    NxtCoxHelper_FormatTime(s, sizeof(s), 6001);
    CHECK(strcmp(s, "01:00:00") == 0, "an hour, the odd tick dropped");
}

/* ---- the paint against the picture ----------------------------------- */

static uint32_t*
load_png(char const* path, int* w, int* h)
{
    int n = 0;
    unsigned char* rgba = stbi_load(path, w, h, &n, 4);
    uint32_t* px;

    if( !rgba )
        return NULL;
    px = malloc((size_t)*w * (size_t)*h * sizeof(*px));
    if( !px )
        abort();
    for( int i = 0; i < *w * *h; i++ )
    {
        unsigned char const* p = rgba + 4 * i;
        px[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    }
    stbi_image_free(rgba);
    return px;
}

static void*
load_file(char const* path, size_t* size)
{
    FILE* f = fopen(path, "rb");
    void* bytes;
    long len;

    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    bytes = malloc((size_t)len + 1);
    if( !bytes )
        abort();
    if( fread(bytes, 1, (size_t)len, f) != (size_t)len )
        abort();
    fclose(f);
    *size = (size_t)len;
    return bytes;
}

static void
test_paint_matches_the_picture(void)
{
    static struct
    {
        char const* kind;
        char const* name;
        char letter;
    } const PICTURE[] = {
        { "Corridor", "Scavenger", 'A' }, { "Combat", "Guardians", 'C' },
        { "Resource", "Bats", 'B' },      { "Combat", "Muttadile", 'C' },
        { "Puzzle", "Tightrope", 'C' },   { "Corridor", "Floor end", 'C' },
        { "Combat", "Tekton", 'A' },      { "Corridor", "Scavenger", 'A' },
        { "Combat", "Mystics", 'B' },     { "Resource", "Fish", 'A' },
        { "Corridor", "Scavenger", 'C' },
    };
    struct PluginDraw_Atlas atlas;
    struct NxtCoxHelperView view;
    struct NxtCoxHelperArt art;
    uint32_t* wiki;
    uint32_t* box;
    size_t ini_size = 0;
    void* ini;
    int wiki_w = 0, wiki_h = 0;
    int w, h;
    int wrong = 0;

    memset(&atlas, 0, sizeof(atlas));
    memset(&art, 0, sizeof(art));
    ini = load_file(ASSETS "text.ini", &ini_size);
    CHECK(ini != NULL, "the shipped p11 metrics");
    CHECK(ini && PluginDraw_AtlasParse(&atlas, ini, ini_size), "the metrics parse");
    atlas.px = load_png(ASSETS "text.png", &atlas.w, &atlas.h);
    art.plate_px = load_png(ASSETS "plate.png", &art.plate_w, &art.plate_h);
    art.arrow_px = load_png(ASSETS "arrow.png", &art.arrow_w, &art.arrow_h);
    wiki = load_png(WIKI, &wiki_w, &wiki_h);
    CHECK(atlas.px && art.plate_px && art.arrow_px && wiki, "every picture loads");
    if( !atlas.px || !art.plate_px || !art.arrow_px || !wiki )
        return;
    art.text = &atlas;

    memset(&view, 0, sizeof(view));
    view.room_count = (int)(sizeof(PICTURE) / sizeof(PICTURE[0]));
    for( int i = 0; i < view.room_count; i++ )
    {
        snprintf(view.rooms[i].kind, sizeof(view.rooms[i].kind), "%s", PICTURE[i].kind);
        snprintf(view.rooms[i].name, sizeof(view.rooms[i].name), "%s", PICTURE[i].name);
        view.rooms[i].letter = PICTURE[i].letter;
    }
    snprintf(view.total, sizeof(view.total), "0");
    snprintf(view.yours, sizeof(view.yours), "0");
    NxtCoxHelper_FormatTime(view.time, sizeof(view.time), 0);

    w = NxtCoxHelper_Width();
    h = NxtCoxHelper_Height(&view);
    CHECK(w == wiki_w && h == wiki_h, "the picture's size");
    if( w != wiki_w || h != wiki_h )
    {
        fprintf(stderr, "  ours %dx%d, the picture %dx%d\n", w, h, wiki_w, wiki_h);
        return;
    }
    box = calloc((size_t)w * (size_t)h, sizeof(*box));
    if( !box )
        abort();
    NxtCoxHelper_Paint(&view, &art, box, w, h);

    for( int y = 0; y < h; y++ )
    {
        for( int x = 0; x < w; x++ )
        {
            uint32_t const p = box[y * w + x];
            int const a = (int)(p >> 24);
            /* Over black, rounded: what the picture was taken over. */
            int const r = (((p >> 16) & 0xFF) * a + 127) / 255;
            int const g = (((p >> 8) & 0xFF) * a + 127) / 255;
            int const b = ((p & 0xFF) * a + 127) / 255;
            uint32_t const ours = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            uint32_t const theirs = wiki[y * w + x] & 0x00FFFFFFu;
            if( ours != theirs )
            {
                if( wrong < 12 )
                    fprintf(stderr, "  (%d,%d) ours %06x, the picture %06x\n", x, y, ours, theirs);
                wrong++;
            }
        }
    }
    fprintf(stderr, "paint vs the picture: %d of %d pixels differ\n", wrong, w * h);
    CHECK(wrong == 0, "the paint IS the picture");
    free(box);
    free(wiki);
    free(ini);
}

int
main(void)
{
    test_table();
    test_walk();
    test_reroll();
    test_format();
    test_paint_matches_the_picture();
    if( g_failures )
    {
        fprintf(stderr, "nxt_cox_helper_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("nxt_cox_helper_test: ok\n");
    return 0;
}
