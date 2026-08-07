/*
 * fontbake — turn cache fonts into a compiled-in `struct ToriDraw_Font`.
 *
 * The debug overlay has to draw before a cache is open (and on builds that ship
 * without one), so its fonts cannot come from `ToriRS_FontFromDat2Archives` at
 * runtime. This decodes the same two inputs that path decodes — the fonts-table
 * metrics blob and the same-id sprites-table glyph pack — and writes a C file
 * whose `struct ToriDraw_Font` is *fully* statically initialised: glyph masks
 * point into one `static const` blob, and the derived tables (`charcodeset`,
 * `draw_width`) are baked too, so there is no init step and no allocation.
 *
 * Archive ids are arguments, never constants in here. Which archive is which
 * font is a property of the cache, so it belongs in the build recipe that names
 * the cache, next to the cache path.
 *
 * Usage:
 *   fontbake --rev NAME <cache_dir> --list [--probe name,name,...]
 *   fontbake --rev NAME <cache_dir> --font ARCHIVE=Symbol [--font ...]
 *            --out out.c [--header out.h] [--metrics metrics.h] [--prefix Prefix]
 *
 * `--metrics` writes a second header carrying only the advance tables, as
 * plain `static const int` with nothing included. That is what lets the module
 * doing the layout measure its own text without linking a font implementation.
 *
 * --list prints every archive in the fonts table with its metrics size, ascent
 * and glyph-pack count. `--probe` hashes each comma-separated name with the
 * dat2 archive-name hash and reports which listed archive it resolves to, which
 * is how an id gets confirmed against the cache rather than against prose.
 */

#include "asset_access.h"
#include "tool_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Glyph order of the 94-glyph RS charset, mirroring ToriDraw's table. Baked
 * fonts are consumed through `struct ToriDraw_Font`, whose glyph slots are
 * indexed in exactly this order, so the two must not drift. */
#define FONT_GLYPH_COUNT 94
#define FONT_ADVANCE_ONLY_GLYPH FONT_GLYPH_COUNT

static const unsigned short FONT_CHARSET[FONT_GLYPH_COUNT] = {
    'A',    'B', 'C',  'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',  'N', 'O', 'P',
    'Q',    'R', 'S',  'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c',  'd', 'e', 'f',
    'g',    'h', 'i',  'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',  't', 'u', 'v',
    'w',    'x', 'y',  'z', '0', '1', '2', '3', '4', '5', '6', '7', '8',  '9', '!', '"',
    0x00A3, '$', '%',  '^', '&', '*', '(', ')', '-', '_', '=', '+', '[',  '{', ']', '}',
    ';',    ':', '\'', '@', '#', '~', ',', '<', '.', '>', '/', '?', '\\', ' '
};

struct BakeGlyph
{
    int width;
    int height;
    int offset_x;
    int offset_y;
    int advance;
    int blob_offset;
};

struct BakeFont
{
    int archive_id;
    char symbol[64];
    struct BakeGlyph glyphs[FONT_GLYPH_COUNT];
    int advance_only;
    int line_height;  /* metrics ascent */
    int line_box;     /* max(offset_y + height), what the minimenu sizes from */
    unsigned char* blob;
    int blob_size;
    int blob_cap;
    signed char charcodeset[256];
    int draw_width[256];
};

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage:\n"
        "  %s --rev NAME <cache_dir> --list [--probe name,name,...]\n"
        "  %s --rev NAME <cache_dir> --font ARCHIVE=Symbol [--font ...]\n"
        "         --out out.c [--header out.h] [--metrics metrics.h] [--prefix Prefix]\n"
        "\n"
        "  --out      the struct ToriDraw_Font bake (needs toridraw_font.h)\n"
        "  --header   its accessors\n"
        "  --metrics  advance tables only: no includes, no types, no linkage\n",
        argv0,
        argv0);
}

static int
font_index_of_char(unsigned char c)
{
    for( int i = 0; i < FONT_GLYPH_COUNT; i++ )
        if( (FONT_CHARSET[i] & 0xFF) == c )
            return i;
    return -1;
}

/* ---- cache reads -------------------------------------------------------- */

/*
 * The fonts-table archive holds one file: the metrics blob. Everything else
 * about the font — the glyph bitmaps — lives in the sprites table under the
 * same archive id, which is the pairing `ToriRS_FontFromDat2Archives` relies on
 * too.
 */
static int
load_font_metrics(
    struct Tool_Dat2Cache* c,
    int archive_id,
    struct RSCache_Dat2FontMetrics* out,
    int* out_blob_size)
{
    int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_FONTS);
    struct Tool_Bytes raw = { 0 };
    struct RSCache_FileList* fl;
    int ok = 0;

    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return 0;
    if( !tool_dat2_archive_bytes(c, table, archive_id, &raw) || raw.size <= 0 )
        return 0;

    /* file_count is 1 for every fonts archive seen; decode as a one-file list
     * so a multi-file group would be read correctly rather than as raw bytes. */
    fl = RSCache_FileListNewFromDecode((char*)raw.data, raw.size, 1);
    if( fl && fl->file_count > 0 && fl->files[0] )
    {
        if( out_blob_size )
            *out_blob_size = fl->file_sizes[0];
        ok = RSCache_Dat2FontMetricsDecodeProfile(
            &c->profile, (const unsigned char*)fl->files[0], fl->file_sizes[0], out);
    }
    RSCache_FileListFree(fl);
    tool_bytes_free(&raw);
    return ok;
}

static struct RSCache_Dat2SpritePack*
load_glyph_pack(
    struct Tool_Dat2Cache* c,
    int archive_id)
{
    int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_SPRITES);
    struct Tool_Bytes raw = { 0 };
    struct RSCache_Dat2SpritePack* pack;

    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return NULL;
    if( !tool_dat2_archive_bytes(c, table, archive_id, &raw) || raw.size <= 0 )
        return NULL;

    /* FLAG_NONE, not NORMALIZE: normalising rewrites crop_* to the memory size
     * and zeroes the offsets, and the offsets are exactly what places a glyph
     * on its baseline. */
    pack = RSCache_Dat2SpritePackNewDecode(raw.data, raw.size, RSCACHE_SPRITELOAD_FLAG_NONE);
    tool_bytes_free(&raw);
    return pack;
}

/* ---- bake --------------------------------------------------------------- */

static int
blob_append(
    struct BakeFont* f,
    const unsigned char* src,
    int len)
{
    if( f->blob_size + len > f->blob_cap )
    {
        int next = f->blob_cap ? f->blob_cap * 2 : 8192;
        unsigned char* grown;
        while( next < f->blob_size + len )
            next *= 2;
        grown = realloc(f->blob, (size_t)next);
        if( !grown )
            return -1;
        f->blob = grown;
        f->blob_cap = next;
    }
    memcpy(f->blob + f->blob_size, src, (size_t)len);
    f->blob_size += len;
    return f->blob_size - len;
}

/*
 * Populate one BakeFont from a decoded metrics blob + glyph pack.
 *
 * This is font_new_from_dat2_metrics_and_sprite_pack (torirs_font_from_rscache.c)
 * written to a file instead of to the heap, and the fallbacks are the same ones
 * on purpose: a bake that filled a gap differently from the runtime decoder
 * would render differently from the cache-loaded font it is standing in for.
 */
static int
bake_font(
    struct BakeFont* f,
    const struct RSCache_Dat2FontMetrics* metrics,
    const struct RSCache_Dat2SpritePack* pack)
{
    int drawable = 0;

    f->line_height = metrics->ascent;

    for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
    {
        int cp = (int)FONT_CHARSET[gi];
        const struct RSCache_Dat2Sprite* sprite;
        int gw, gh, off;
        unsigned char* mask;

        f->glyphs[gi].blob_offset = -1;
        if( cp < 0 || cp >= pack->count )
            continue;

        sprite = &pack->sprites[cp];
        gw = sprite->crop_width;
        gh = sprite->crop_height;
        if( gw <= 0 || gh <= 0 || !sprite->pixel_alphas )
            continue;

        /* Alpha is binarised to 0/255, same as the runtime decoder: the glyph
         * blitter tests for non-zero, and 0/255 keeps a baked font byte-equal
         * to a cache-loaded one under a memcmp. */
        mask = malloc((size_t)gw * (size_t)gh);
        if( !mask )
            return 0;
        for( int j = 0; j < gw * gh; j++ )
            mask[j] = sprite->pixel_alphas[j] != 0 ? 255 : 0;

        off = blob_append(f, mask, gw * gh);
        free(mask);
        if( off < 0 )
            return 0;

        f->glyphs[gi].blob_offset = off;
        f->glyphs[gi].width = gw;
        f->glyphs[gi].height = gh;
        f->glyphs[gi].offset_x = sprite->offset_x;
        f->glyphs[gi].offset_y = sprite->offset_y;
        f->glyphs[gi].advance = metrics->advance[cp];
        if( f->glyphs[gi].advance <= 0 )
            f->glyphs[gi].advance = gw + 2;
        drawable++;

        if( sprite->offset_y + gh > f->line_box )
            f->line_box = sprite->offset_y + gh;
    }

    if( metrics->advance[(unsigned char)' '] > 0 )
        f->advance_only = metrics->advance[(unsigned char)' '];

    if( f->line_height <= 0 )
        for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
            if( f->glyphs[gi].height > f->line_height )
                f->line_height = f->glyphs[gi].height;
    if( f->line_box <= 0 )
        f->line_box = f->line_height;

    /* charcodeset + draw_width, precomputed here so the baked struct needs no
     * init call. Mirrors ToriDraw_FontInitCharcodeset / FinishDrawWidths. */
    for( int i = 0; i < 256; i++ )
    {
        int gi = font_index_of_char((unsigned char)i);
        if( gi == -1 )
            gi = font_index_of_char(' ');
        if( gi < 0 || gi >= FONT_GLYPH_COUNT )
            gi = FONT_GLYPH_COUNT - 1;
        f->charcodeset[i] = (signed char)gi;
    }
    f->charcodeset[(unsigned char)' '] = (signed char)FONT_ADVANCE_ONLY_GLYPH;
    f->charcodeset[(unsigned char)'|'] = (signed char)FONT_ADVANCE_ONLY_GLYPH;

    {
        int fallback = f->glyphs[8].advance > 0 ? f->glyphs[8].advance : 4;
        if( f->glyphs[93].advance < 4 )
            f->glyphs[93].advance = fallback;
        if( f->advance_only <= 0 )
            f->advance_only = fallback;
        for( int i = 0; i < 256; i++ )
        {
            int gi = (unsigned char)f->charcodeset[i];
            f->draw_width[i] =
                gi == FONT_ADVANCE_ONLY_GLYPH ? f->advance_only : f->glyphs[gi].advance;
        }
        f->draw_width[(unsigned char)' '] = f->advance_only;
        f->draw_width[(unsigned char)'|'] = f->advance_only;
    }

    return drawable > 0;
}

/* ---- emit --------------------------------------------------------------- */

static void
emit_blob(
    FILE* out,
    const struct BakeFont* f)
{
    fprintf(
        out,
        "/* Glyph coverage masks, 0 or 255 per pixel, row-major, concatenated in\n"
        " * charset order. %d bytes. */\n"
        "static const unsigned char %s_glyph_bits[%d] = {",
        f->blob_size,
        f->symbol,
        f->blob_size);
    for( int i = 0; i < f->blob_size; i++ )
    {
        if( i % 24 == 0 )
            fprintf(out, "\n    ");
        fprintf(out, "%d,", f->blob[i]);
    }
    fprintf(out, "\n};\n\n");
}

static void
emit_font(
    FILE* out,
    const struct BakeFont* f)
{
    emit_blob(out, f);

    fprintf(out, "static struct ToriDraw_Font %s_font = {\n", f->symbol);

    fprintf(out, "    .glyph_alpha = {");
    for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
    {
        if( gi % 4 == 0 )
            fprintf(out, "\n        ");
        if( f->glyphs[gi].blob_offset < 0 )
            fprintf(out, "NULL, ");
        else
            fprintf(
                out,
                "(unsigned char*)%s_glyph_bits + %d, ",
                f->symbol,
                f->glyphs[gi].blob_offset);
    }
    fprintf(out, "\n    },\n");

    fprintf(out, "    .glyph_width = {");
    for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
        fprintf(out, "%s%d,", gi % 24 == 0 ? "\n        " : " ", f->glyphs[gi].width);
    fprintf(out, "\n    },\n");

    fprintf(out, "    .glyph_height = {");
    for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
        fprintf(out, "%s%d,", gi % 24 == 0 ? "\n        " : " ", f->glyphs[gi].height);
    fprintf(out, "\n    },\n");

    fprintf(out, "    .offset_x = {");
    for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
        fprintf(out, "%s%d,", gi % 24 == 0 ? "\n        " : " ", f->glyphs[gi].offset_x);
    fprintf(out, "\n    },\n");

    fprintf(out, "    .offset_y = {");
    for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
        fprintf(out, "%s%d,", gi % 24 == 0 ? "\n        " : " ", f->glyphs[gi].offset_y);
    fprintf(out, "\n    },\n");

    fprintf(out, "    /* [%d] is the advance-only slot (space, pipe). */\n", FONT_GLYPH_COUNT);
    fprintf(out, "    .advance = {");
    for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
        fprintf(out, "%s%d,", gi % 24 == 0 ? "\n        " : " ", f->glyphs[gi].advance);
    fprintf(out, " %d,\n    },\n", f->advance_only);

    fprintf(out, "    .draw_width = {");
    for( int i = 0; i < 256; i++ )
        fprintf(out, "%s%d,", i % 24 == 0 ? "\n        " : " ", f->draw_width[i]);
    fprintf(out, "\n    },\n");

    fprintf(out, "    .line_height = %d,\n", f->line_height);

    fprintf(out, "    .charcodeset = {");
    for( int i = 0; i < 256; i++ )
        fprintf(out, "%s%d,", i % 24 == 0 ? "\n        " : " ", f->charcodeset[i]);
    fprintf(out, "\n    },\n");

    fprintf(out, "};\n\n");
}

static void
emit_header(
    FILE* out,
    const char* guard,
    const char* prefix,
    const struct BakeFont* fonts,
    int font_count,
    const char* rev,
    const char* cache_dir)
{
    fprintf(
        out,
        "/*\n"
        " * GENERATED by 3rd/rscache/tools/fontbake — do not edit.\n"
        " *\n"
        " * Source cache: %s (--rev %s)\n"
        " *\n"
        " * Fonts baked in this file:\n",
        cache_dir,
        rev);
    for( int i = 0; i < font_count; i++ )
        fprintf(
            out,
            " *   %-16s fonts/sprites archive %d  ascent %d  line box %d  %d glyph bytes\n",
            fonts[i].symbol,
            fonts[i].archive_id,
            fonts[i].line_height,
            fonts[i].line_box,
            fonts[i].blob_size);
    fprintf(
        out,
        " *\n"
        " * The returned fonts are statically allocated and live for the life of\n"
        " * the process. Never pass one to ToriDraw_FontFree: it frees every\n"
        " * glyph_alpha pointer, and these point into a const blob.\n"
        " */\n"
        "#ifndef %s\n"
        "#define %s\n"
        "\n"
        "#include \"toridraw_font.h\"\n"
        "\n",
        guard,
        guard);
    for( int i = 0; i < font_count; i++ )
        fprintf(
            out,
            "/** Cache font archive %d. Ascent %d, glyph line box %d. */\n"
            "struct ToriDraw_Font*\n"
            "%s_%s(void);\n"
            "\n",
            fonts[i].archive_id,
            fonts[i].line_height,
            fonts[i].line_box,
            prefix,
            fonts[i].symbol);
    fprintf(out, "#endif\n");
}

/*
 * The metrics-only header. Same numbers as the full bake, minus every type
 * that would drag a dependency in: plain `static const int` tables and nothing
 * included. A leaf module can measure text with this alone — which is the
 * point, since the module that lays the overlay out must not link a renderer.
 *
 * `advance_px[c]` is ToriDraw_Font.draw_width[c] with the same `<= 0 -> 4`
 * clamp font_glyph_advance applies at measure time, so summing it over a
 * markup-free string reproduces font_measure_range exactly.
 */
static void
emit_metrics_header(
    FILE* out,
    const char* guard,
    const char* prefix,
    const struct BakeFont* fonts,
    int font_count,
    const char* rev,
    const char* cache_dir)
{
    fprintf(
        out,
        "/*\n"
        " * GENERATED by 3rd/rscache/tools/fontbake — do not edit.\n"
        " *\n"
        " * Source cache: %s (--rev %s)\n"
        " *\n"
        " * Advance tables only: no includes, no types, no linkage. Include this\n"
        " * to measure text without depending on a font implementation.\n"
        " *\n"
        " * <prefix>_<Symbol>_advance_px[c] is the pen advance of byte `c`, so a\n"
        " * string's pixel width is the sum over its bytes. Markup tokens\n"
        " * (<col=...> and friends) are NOT recognised here — this measures the\n"
        " * literal bytes, which is what plain overlay text needs.\n"
        " */\n"
        "#ifndef %s\n"
        "#define %s\n"
        "\n",
        cache_dir,
        rev,
        guard,
        guard);

    for( int i = 0; i < font_count; i++ )
    {
        fprintf(
            out,
            "/* %s: cache font archive %d. */\n"
            "/** Baseline offset from the top of a line box (font ascent). */\n"
            "#define %s_%s_LINE_HEIGHT %d\n"
            "/** Row pitch: the tallest glyph's bottom edge. */\n"
            "#define %s_%s_LINE_BOX %d\n"
            "\n"
            "static const int %s_%s_advance_px[256] = {\n",
            fonts[i].symbol,
            fonts[i].archive_id,
            prefix,
            fonts[i].symbol,
            fonts[i].line_height,
            prefix,
            fonts[i].symbol,
            fonts[i].line_box,
            prefix,
            fonts[i].symbol);
        for( int c = 0; c < 256; c++ )
        {
            int adv = fonts[i].draw_width[c];
            if( adv <= 0 )
                adv = 4;
            fprintf(out, "%s%3d,", (c % 16) == 0 ? "    " : "", adv);
            if( (c % 16) == 15 )
                fprintf(out, "\n");
        }
        fprintf(out, "};\n\n");
    }

    fprintf(out, "#endif\n");
}

/* ---- list --------------------------------------------------------------- */

static void
list_fonts(
    struct Tool_Dat2Cache* c,
    const char* probe_csv)
{
    int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_FONTS);
    struct RSCache_ReferenceTable* rt;

    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT || !c->disk->tables[table] )
    {
        printf("fonts table absent in this cache\n");
        return;
    }
    rt = c->disk->tables[table];
    printf(
        "fonts table %d: %d archives  (reference table flags 0x%x)\n",
        table,
        rt->id_count,
        rt->flags);
    printf("%6s %12s %8s %7s %8s %9s\n", "id", "name_hash", "metrics", "ascent", "glyphs",
           "line_box");

    for( int i = 0; i < rt->id_count; i++ )
    {
        int id = rt->ids[i];
        struct RSCache_Dat2FontMetrics metrics;
        struct RSCache_Dat2SpritePack* pack;
        int blob = 0;
        int glyphs = 0;
        int line_box = 0;
        int identifier = 0;

        for( int a = 0; a < rt->archive_count; a++ )
            if( rt->archives[a].index == id )
            {
                identifier = rt->archives[a].identifier;
                break;
            }

        if( !load_font_metrics(c, id, &metrics, &blob) )
        {
            printf("%6d %12d %8s\n", id, identifier, "<no metrics>");
            continue;
        }
        pack = load_glyph_pack(c, id);
        if( pack )
        {
            for( int gi = 0; gi < FONT_GLYPH_COUNT; gi++ )
            {
                int cp = (int)FONT_CHARSET[gi];
                if( cp < pack->count && pack->sprites[cp].crop_width > 0 &&
                    pack->sprites[cp].crop_height > 0 )
                {
                    int bottom = pack->sprites[cp].offset_y + pack->sprites[cp].crop_height;
                    glyphs++;
                    if( bottom > line_box )
                        line_box = bottom;
                }
            }
            RSCache_Dat2SpritePackFree(pack);
        }
        printf(
            "%6d %12d %8d %7d %8d %9d\n", id, identifier, blob, metrics.ascent, glyphs, line_box);
    }

    if( probe_csv )
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", probe_csv);
        printf("\nname probe (dat2 archive-name hash against the table above):\n");
        for( char* tok = buf; tok && *tok; )
        {
            char* comma = strchr(tok, ',');
            int hash;
            int hit = -1;
            if( comma )
                *comma = '\0';
            hash = RSCache_ArchiveNameHashDat2(tok);
            for( int a = 0; a < rt->archive_count; a++ )
                if( rt->archives[a].identifier == hash )
                {
                    hit = rt->archives[a].index;
                    break;
                }
            if( hit >= 0 )
                printf("  %-20s hash %12d -> archive %d\n", tok, hash, hit);
            else
                printf("  %-20s hash %12d -> (no archive carries this name)\n", tok, hash);
            tok = comma ? comma + 1 : NULL;
        }
    }
}

/* ---- main --------------------------------------------------------------- */

#define MAX_FONTS 8

int
main(int argc, char** argv)
{
    const char* cache_dir = NULL;
    const char* rev = NULL;
    const char* out_path = NULL;
    const char* header_path = NULL;
    const char* metrics_path = NULL;
    const char* prefix = "ToriDbgFont";
    const char* probe = NULL;
    int do_list = 0;
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    struct BakeFont fonts[MAX_FONTS];
    int font_count = 0;
    FILE* out;
    int rc = 0;

    memset(fonts, 0, sizeof(fonts));
    for( int i = 0; i < MAX_FONTS; i++ )
        fonts[i].archive_id = -1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_path = argv[++i];
        else if( strcmp(argv[i], "--header") == 0 && i + 1 < argc )
            header_path = argv[++i];
        else if( strcmp(argv[i], "--metrics") == 0 && i + 1 < argc )
            metrics_path = argv[++i];
        else if( strcmp(argv[i], "--prefix") == 0 && i + 1 < argc )
            prefix = argv[++i];
        else if( strcmp(argv[i], "--probe") == 0 && i + 1 < argc )
            probe = argv[++i];
        else if( strcmp(argv[i], "--list") == 0 )
            do_list = 1;
        else if( strcmp(argv[i], "--font") == 0 && i + 1 < argc )
        {
            const char* spec = argv[++i];
            const char* eq = strchr(spec, '=');
            if( !eq || font_count >= MAX_FONTS )
            {
                fprintf(stderr, "bad --font %s (want ARCHIVE=Symbol)\n", spec);
                return 2;
            }
            fonts[font_count].archive_id = atoi(spec);
            snprintf(fonts[font_count].symbol, sizeof(fonts[font_count].symbol), "%s", eq + 1);
            font_count++;
        }
        else if( argv[i][0] != '-' )
            cache_dir = argv[i];
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if( !cache_dir || !rev || (!do_list && font_count == 0) )
    {
        usage(argv[0]);
        return 2;
    }
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
        return 2;
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "could not open dat2 cache at %s\n", cache_dir);
        return 1;
    }

    if( do_list )
        list_fonts(&cache, probe);

    if( font_count == 0 )
    {
        tool_dat2_close(&cache);
        return 0;
    }
    if( !out_path )
    {
        fprintf(stderr, "--font needs --out\n");
        tool_dat2_close(&cache);
        return 2;
    }

    for( int i = 0; i < font_count; i++ )
    {
        struct RSCache_Dat2FontMetrics metrics;
        struct RSCache_Dat2SpritePack* pack;
        int blob = 0;

        if( !load_font_metrics(&cache, fonts[i].archive_id, &metrics, &blob) )
        {
            fprintf(stderr, "archive %d: no font metrics\n", fonts[i].archive_id);
            rc = 1;
            goto done;
        }
        pack = load_glyph_pack(&cache, fonts[i].archive_id);
        if( !pack )
        {
            fprintf(stderr, "archive %d: no glyph sprite pack\n", fonts[i].archive_id);
            rc = 1;
            goto done;
        }
        if( !bake_font(&fonts[i], &metrics, pack) )
        {
            fprintf(stderr, "archive %d: no drawable glyphs\n", fonts[i].archive_id);
            RSCache_Dat2SpritePackFree(pack);
            rc = 1;
            goto done;
        }
        RSCache_Dat2SpritePackFree(pack);
        fprintf(
            stderr,
            "baked %s from archive %d: ascent %d, line box %d, %d glyph bytes\n",
            fonts[i].symbol,
            fonts[i].archive_id,
            fonts[i].line_height,
            fonts[i].line_box,
            fonts[i].blob_size);
    }

    out = fopen(out_path, "wb");
    if( !out )
    {
        fprintf(stderr, "cannot write %s\n", out_path);
        rc = 1;
        goto done;
    }
    fprintf(
        out,
        "/*\n"
        " * GENERATED by 3rd/rscache/tools/fontbake — do not edit.\n"
        " *\n"
        " * Source cache: %s (--rev %s)\n"
        " * Regenerate:   make -C 3rd/rscache/tools fontbake && \\\n"
        " *               3rd/rscache/tools/fontbake/fontbake --rev %s <cache> \\\n",
        cache_dir,
        rev,
        rev);
    for( int i = 0; i < font_count; i++ )
        fprintf(out, " *                   --font %d=%s \\\n", fonts[i].archive_id, fonts[i].symbol);
    fprintf(
        out,
        " *                   --out %s --header %s\n"
        " *\n"
        " * Each font is fully statically initialised — glyph masks point into the\n"
        " * const blob above them and the derived tables are baked, so there is no\n"
        " * init step and no allocation. Never ToriDraw_FontFree one of these.\n"
        " */\n"
        "\n",
        out_path,
        header_path ? header_path : "(none)");
    if( header_path )
    {
        const char* base = header_path;
        for( const char* p = header_path; *p; p++ )
            if( *p == '/' || *p == '\\' )
                base = p + 1;
        fprintf(out, "#include \"%s\"\n\n", base);
    }
    else
    {
        fprintf(out, "#include \"toridraw_font.h\"\n\n");
    }
    fprintf(out, "#include <stddef.h>\n\n");

    for( int i = 0; i < font_count; i++ )
    {
        fprintf(
            out,
            "/* ---- %s: cache font archive %d ------------------------------- */\n\n",
            fonts[i].symbol,
            fonts[i].archive_id);
        emit_font(out, &fonts[i]);
        fprintf(
            out,
            "struct ToriDraw_Font*\n"
            "%s_%s(void)\n"
            "{\n"
            "    return &%s_font;\n"
            "}\n\n",
            prefix,
            fonts[i].symbol,
            fonts[i].symbol);
    }
    fclose(out);
    printf("wrote %s\n", out_path);

    if( header_path )
    {
        char guard[128];
        int gi = 0;
        const char* base = header_path;
        for( const char* p = header_path; *p; p++ )
            if( *p == '/' || *p == '\\' )
                base = p + 1;
        for( const char* p = base; *p && gi < (int)sizeof(guard) - 1; p++ )
            guard[gi++] = (*p >= 'a' && *p <= 'z')                                  ? (char)(*p - 'a' + 'A')
                          : ((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) ? *p
                                                                                   : '_';
        guard[gi] = '\0';

        out = fopen(header_path, "wb");
        if( !out )
        {
            fprintf(stderr, "cannot write %s\n", header_path);
            rc = 1;
            goto done;
        }
        emit_header(out, guard, prefix, fonts, font_count, rev, cache_dir);
        fclose(out);
        printf("wrote %s\n", header_path);
    }

    if( metrics_path )
    {
        char guard[128];
        int gi = 0;
        const char* base = metrics_path;
        for( const char* p = metrics_path; *p; p++ )
            if( *p == '/' || *p == '\\' )
                base = p + 1;
        for( const char* p = base; *p && gi < (int)sizeof(guard) - 1; p++ )
            guard[gi++] = (*p >= 'a' && *p <= 'z')                                  ? (char)(*p - 'a' + 'A')
                          : ((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) ? *p
                                                                                   : '_';
        guard[gi] = '\0';

        out = fopen(metrics_path, "wb");
        if( !out )
        {
            fprintf(stderr, "cannot write %s\n", metrics_path);
            rc = 1;
            goto done;
        }
        emit_metrics_header(out, guard, prefix, fonts, font_count, rev, cache_dir);
        fclose(out);
        printf("wrote %s\n", metrics_path);
    }

done:
    for( int i = 0; i < font_count; i++ )
        free(fonts[i].blob);
    tool_dat2_close(&cache);
    return rc;
}
