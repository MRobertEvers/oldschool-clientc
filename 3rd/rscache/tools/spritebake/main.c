/*
 * spritebake — turn cache sprites into compiled-in ARGB arrays.
 *
 * fontbake's sibling, and for the same reason. The editor chrome has to draw
 * before a cache is open, on a cache that failed to open, and on builds that
 * ship without one — so the art it draws itself with cannot be decoded at
 * runtime. This decodes the sprites table once, at build time, against a
 * pinned cache, and writes a C file of `static const uint32_t` ARGB rows.
 *
 * Archive ids are arguments, never constants in here. Which archive is which
 * asset is a property of the cache, so it belongs in the build recipe that
 * names the cache, next to the cache path.
 *
 * Usage:
 *   spritebake --rev NAME <cache_dir> --list [--probe name,name,...]
 *   spritebake --rev NAME <cache_dir> --sprite SPEC [--sprite ...]
 *              --out out.c [--header out.h] [--sheet sheet.bmp] [--prefix Prefix]
 *
 * SPEC is `ARCHIVE[.FRAME]=Symbol`, or `name:NAME[.FRAME]=Symbol` to resolve
 * the archive by its dat2 name hash instead of by number. Both forms are
 * recorded in the generated file's header comment, so a regenerated bake is a
 * reviewable diff rather than a silent one.
 *
 * --list prints every archive in the sprites table with its frame count and
 * first-frame geometry. `--probe` hashes each comma-separated name with the
 * dat2 archive-name hash and reports which archive it resolves to — which is
 * how an id gets confirmed against the cache rather than against prose. Not
 * every interface asset carries a name (plenty are referenced only by number
 * from an interface component), which is exactly why both spellings exist.
 *
 * --sheet writes every baked sprite into one BMP, laid out in a row with a
 * magenta background, so a human can confirm the extraction picked the art it
 * meant to. The C file is the artefact; the sheet is how you check it.
 */

#include "asset_access.h"
#include "tool_profile.h"

#include "bmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Baked sprites per run. Chrome needs a frame set and a few glyphs. */
#define BAKE_MAX_SPRITES 64

struct BakeSprite
{
    int archive_id;
    int frame;
    /** The `name:` spelling this was requested by, or "" when asked by id. */
    char by_name[64];
    char symbol[64];
    int w;
    int h;
    /** w*h ARGB, premultiplied by nothing — straight 0xAARRGGBB. */
    unsigned int* argb;
};

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "spritebake — cache sprites to compiled-in ARGB arrays\n"
        "\n"
        "  %s --rev NAME <cache_dir> --list [--probe name,name,...]\n"
        "  %s --rev NAME <cache_dir> --sprite ARCHIVE[.FRAME]=Symbol [--sprite ...]\n"
        "         --out out.c [--header out.h] [--sheet sheet.bmp] [--prefix Prefix]\n"
        "\n"
        "  --sprite   also accepts name:NAME[.FRAME]=Symbol\n"
        "  --out      the ARGB bake (needs stdint.h only)\n"
        "  --header   its accessors\n"
        "  --sheet    a contact sheet BMP of everything baked, to eyeball it\n",
        argv0,
        argv0);
}

/* ---- decode -------------------------------------------------------------- */

static struct RSCache_Dat2SpritePack*
load_pack(
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

    /* NORMALIZE, unlike fontbake. A glyph is placed by its crop offsets onto a
     * baseline, so fontbake must keep them; chrome art is blitted as a whole
     * image, so the memory-sized frame with the offsets already applied is
     * exactly what a blit wants. */
    pack = RSCache_Dat2SpritePackNewDecode(raw.data, raw.size, RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
    tool_bytes_free(&raw);
    return pack;
}

/**
 * One frame's pixels as ARGB.
 *
 * Palette index 0 is the pack's transparent slot, not a colour — the decoder
 * leaves those pixels as index 0 and the palette's entry for them is
 * meaningless. Writing alpha 0 for index 0 is what keeps a sprite's corners
 * transparent instead of black.
 */
static unsigned int*
frame_to_argb(
    struct RSCache_Dat2SpritePack const* pack,
    int frame,
    int* out_w,
    int* out_h)
{
    struct RSCache_Dat2Sprite const* s;
    unsigned int* argb;
    int n;

    if( !pack || frame < 0 || frame >= pack->count )
        return NULL;
    s = &pack->sprites[frame];
    if( s->width <= 0 || s->height <= 0 || !s->palette_pixels )
        return NULL;

    n = s->width * s->height;
    argb = calloc((size_t)n, sizeof(*argb));
    if( !argb )
        return NULL;

    for( int i = 0; i < n; i++ )
    {
        int idx = s->palette_pixels[i];
        int rgb;
        int alpha;

        if( idx == 0 )
            continue; /* transparent; calloc already left it 0 */
        rgb = (idx < pack->palette_length) ? pack->palette[idx] : 0;
        alpha = s->pixel_alphas ? s->pixel_alphas[i] : 0xFF;
        argb[i] = ((unsigned int)(alpha & 0xFF) << 24) | (unsigned int)(rgb & 0xFFFFFF);
    }

    *out_w = s->width;
    *out_h = s->height;
    return argb;
}

/* ---- list ---------------------------------------------------------------- */

static void
list_sprites(
    struct Tool_Dat2Cache* c,
    const char* probe_csv)
{
    int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_SPRITES);
    struct RSCache_ReferenceTable* rt;

    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT || !c->disk->tables[table] )
    {
        printf("sprites table absent in this cache\n");
        return;
    }
    rt = c->disk->tables[table];
    printf(
        "sprites table %d: %d archives  (reference table flags 0x%x)\n",
        table,
        rt->id_count,
        rt->flags);

    /* The probe is the useful half by far, so it runs first and the full dump
     * is opt-in: this table has thousands of archives and almost none of them
     * carry a name worth reading. */
    if( probe_csv )
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%s", probe_csv);
        printf("\nname probe (dat2 archive-name hash against this table):\n");
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
            {
                struct RSCache_Dat2SpritePack* pack = load_pack(c, hit);
                if( pack && pack->count > 0 )
                    printf(
                        "  %-24s hash %12d -> archive %5d  frames %3d  %dx%d\n",
                        tok, hash, hit, pack->count, pack->sprites[0].width,
                        pack->sprites[0].height);
                else
                    printf(
                        "  %-24s hash %12d -> archive %5d  <undecodable>\n", tok, hash, hit);
                if( pack )
                    RSCache_Dat2SpritePackFree(pack);
            }
            else
                printf("  %-24s hash %12d -> (no archive carries this name)\n", tok, hash);
            tok = comma ? comma + 1 : NULL;
        }
        return;
    }

    printf("%6s %12s %7s %10s\n", "id", "name_hash", "frames", "first");
    for( int i = 0; i < rt->id_count; i++ )
    {
        int id = rt->ids[i];
        int identifier = 0;
        struct RSCache_Dat2SpritePack* pack;

        for( int a = 0; a < rt->archive_count; a++ )
            if( rt->archives[a].index == id )
            {
                identifier = rt->archives[a].identifier;
                break;
            }
        pack = load_pack(c, id);
        if( !pack || pack->count <= 0 )
            printf("%6d %12d %7s\n", id, identifier, "<none>");
        else
            printf(
                "%6d %12d %7d %5dx%-4d\n", id, identifier, pack->count,
                pack->sprites[0].width, pack->sprites[0].height);
        if( pack )
            RSCache_Dat2SpritePackFree(pack);
    }
}

/* ---- emit ---------------------------------------------------------------- */

static void
emit_source(
    FILE* out,
    const char* cache_dir,
    const char* rev,
    const char* prefix,
    const char* header_name,
    struct BakeSprite* sprites,
    int count)
{
    fprintf(
        out,
        "/*\n"
        " * GENERATED by 3rd/rscache/tools/spritebake — do not edit.\n"
        " *\n"
        " * Source cache: %s (--rev %s)\n"
        " *\n"
        " * Sprites baked in this file:\n",
        cache_dir,
        rev);
    for( int i = 0; i < count; i++ )
        fprintf(
            out,
            " *   %-20s archive %5d frame %d  %dx%d%s%s\n",
            sprites[i].symbol,
            sprites[i].archive_id,
            sprites[i].frame,
            sprites[i].w,
            sprites[i].h,
            sprites[i].by_name[0] ? "  name " : "",
            sprites[i].by_name);
    fprintf(
        out,
        " *\n"
        " * Regenerate with:\n"
        " *   3rd/rscache/tools/spritebake/spritebake --rev %s <cache> \\\n",
        rev);
    for( int i = 0; i < count; i++ )
    {
        if( sprites[i].by_name[0] )
            fprintf(
                out, " *       --sprite name:%s.%d=%s \\\n", sprites[i].by_name, sprites[i].frame,
                sprites[i].symbol);
        else
            fprintf(
                out, " *       --sprite %d.%d=%s \\\n", sprites[i].archive_id, sprites[i].frame,
                sprites[i].symbol);
    }
    fprintf(out, " *       --out <this file> --header <its header>\n */\n");

    if( header_name )
        fprintf(out, "#include \"%s\"\n\n", header_name);
    else
        fprintf(out, "#include <stdint.h>\n\n");

    for( int i = 0; i < count; i++ )
    {
        int n = sprites[i].w * sprites[i].h;
        fprintf(
            out, "static const uint32_t %s_%s_argb[%d] = {\n", prefix, sprites[i].symbol, n);
        for( int p = 0; p < n; p++ )
        {
            if( p % 8 == 0 )
                fprintf(out, "   ");
            fprintf(out, " 0x%08X,", sprites[i].argb[p]);
            if( p % 8 == 7 || p == n - 1 )
                fprintf(out, "\n");
        }
        fprintf(out, "};\n\n");
    }

    fprintf(
        out,
        "static const struct %s_Sprite %s_table[%d] = {\n",
        prefix,
        prefix,
        count);
    for( int i = 0; i < count; i++ )
        fprintf(
            out, "    { %d, %d, %s_%s_argb },\n", sprites[i].w, sprites[i].h, prefix,
            sprites[i].symbol);
    fprintf(out, "};\n\n");

    fprintf(
        out,
        "const struct %s_Sprite*\n%s_Get(int slot)\n"
        "{\n"
        "    if( slot < 0 || slot >= %d )\n"
        "        return 0;\n"
        "    return &%s_table[slot];\n"
        "}\n\n"
        "int\n%s_Count(void)\n"
        "{\n"
        "    return %d;\n"
        "}\n",
        prefix,
        prefix,
        count,
        prefix,
        prefix,
        count);
}

static void
emit_header(
    FILE* out,
    const char* cache_dir,
    const char* rev,
    const char* prefix,
    struct BakeSprite* sprites,
    int count)
{
    fprintf(
        out,
        "/*\n"
        " * GENERATED by 3rd/rscache/tools/spritebake — do not edit.\n"
        " *\n"
        " * Source cache: %s (--rev %s)\n"
        " *\n"
        " * The arrays are `static const` and live for the life of the process.\n"
        " * Never free one: they point into .rdata.\n"
        " */\n"
        "#ifndef TORIRS_CHROME_SKIN_BAKED_H\n"
        "#define TORIRS_CHROME_SKIN_BAKED_H\n\n"
        "#include <stdint.h>\n\n"
        "/** One baked image: `w*h` pixels of 0xAARRGGBB, row-major. */\n"
        "struct %s_Sprite\n"
        "{\n"
        "    int w;\n"
        "    int h;\n"
        "    const uint32_t* argb;\n"
        "};\n\n"
        "/** Slot ids, in the order they were baked. */\n"
        "enum %s_Slot\n"
        "{\n",
        cache_dir,
        rev,
        prefix,
        prefix);
    for( int i = 0; i < count; i++ )
        fprintf(out, "    %s_SLOT_%s = %d,\n", prefix, sprites[i].symbol, i);
    fprintf(out, "    %s_SLOT_COUNT = %d\n};\n\n", prefix, count);
    fprintf(
        out,
        "/** The baked image for `slot`, or NULL when the slot is out of range. */\n"
        "const struct %s_Sprite*\n%s_Get(int slot);\n\n"
        "int\n%s_Count(void);\n\n"
        "#endif\n",
        prefix,
        prefix,
        prefix);
}

/** One BMP with every baked sprite in a row, on magenta, to eyeball the bake. */
static void
emit_sheet(
    const char* path,
    struct BakeSprite* sprites,
    int count)
{
    int const pad = 4;
    int total_w = pad;
    int max_h = 0;
    unsigned int* canvas;
    int pen_x = pad;

    for( int i = 0; i < count; i++ )
    {
        total_w += sprites[i].w + pad;
        if( sprites[i].h > max_h )
            max_h = sprites[i].h;
    }
    if( total_w <= 0 || max_h <= 0 )
        return;
    max_h += 2 * pad;

    canvas = malloc((size_t)total_w * (size_t)max_h * sizeof(*canvas));
    if( !canvas )
        return;
    for( int i = 0; i < total_w * max_h; i++ )
        canvas[i] = 0xFFFF00FF; /* magenta: nothing in RS chrome is this colour */

    for( int i = 0; i < count; i++ )
    {
        for( int y = 0; y < sprites[i].h; y++ )
            for( int x = 0; x < sprites[i].w; x++ )
            {
                unsigned int px = sprites[i].argb[y * sprites[i].w + x];
                if( (px >> 24) == 0 )
                    continue; /* transparent: let the magenta show through */
                canvas[(y + pad) * total_w + (pen_x + x)] = px;
            }
        pen_x += sprites[i].w + pad;
    }

    bmp_write_file(path, (int*)canvas, total_w, max_h);
    printf("wrote %s (%dx%d, %d sprites)\n", path, total_w, max_h, count);
    free(canvas);
}

/* ---- main ---------------------------------------------------------------- */

int
main(int argc, char** argv)
{
    const char* cache_dir = NULL;
    const char* rev = NULL;
    const char* out_path = NULL;
    const char* header_path = NULL;
    const char* sheet_path = NULL;
    const char* prefix = "ToriRSChromeSkin";
    const char* probe = NULL;
    int do_list = 0;
    struct BakeSprite sprites[BAKE_MAX_SPRITES];
    int sprite_count = 0;
    struct Tool_Dat2Cache cache = { 0 };
    struct RSCache profile;
    struct RSCache_ReferenceTable* rt = NULL;
    int table;

    memset(sprites, 0, sizeof(sprites));

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_path = argv[++i];
        else if( strcmp(argv[i], "--header") == 0 && i + 1 < argc )
            header_path = argv[++i];
        else if( strcmp(argv[i], "--sheet") == 0 && i + 1 < argc )
            sheet_path = argv[++i];
        else if( strcmp(argv[i], "--prefix") == 0 && i + 1 < argc )
            prefix = argv[++i];
        else if( strcmp(argv[i], "--probe") == 0 && i + 1 < argc )
            probe = argv[++i];
        else if( strcmp(argv[i], "--list") == 0 )
            do_list = 1;
        else if( strcmp(argv[i], "--sprite") == 0 && i + 1 < argc )
        {
            /* ARCHIVE[.FRAME]=Symbol, or name:NAME[.FRAME]=Symbol. */
            char spec[192];
            char* eq;
            char* dot;
            struct BakeSprite* s;

            if( sprite_count >= BAKE_MAX_SPRITES )
            {
                fprintf(stderr, "too many --sprite (max %d)\n", BAKE_MAX_SPRITES);
                return 1;
            }
            snprintf(spec, sizeof(spec), "%s", argv[++i]);
            eq = strchr(spec, '=');
            if( !eq )
            {
                fprintf(stderr, "bad --sprite %s (want ARCHIVE[.FRAME]=Symbol)\n", spec);
                return 1;
            }
            *eq = '\0';
            s = &sprites[sprite_count++];
            snprintf(s->symbol, sizeof(s->symbol), "%s", eq + 1);

            dot = strrchr(spec, '.');
            if( dot )
            {
                *dot = '\0';
                s->frame = atoi(dot + 1);
            }
            if( strncmp(spec, "name:", 5) == 0 )
            {
                snprintf(s->by_name, sizeof(s->by_name), "%s", spec + 5);
                s->archive_id = -1; /* resolved once the cache is open */
            }
            else
                s->archive_id = atoi(spec);
        }
        else if( !cache_dir )
            cache_dir = argv[i];
        else
        {
            usage(argv[0]);
            return 1;
        }
    }

    if( !cache_dir || !rev || (!do_list && sprite_count == 0) )
    {
        usage(argv[0]);
        return 1;
    }
    if( sprite_count > 0 && !out_path )
    {
        fprintf(stderr, "--sprite needs --out\n");
        return 1;
    }

    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
    {
        fprintf(stderr, "unknown --rev %s\n", rev);
        return 1;
    }
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "cannot open cache %s (--rev %s)\n", cache_dir, rev);
        return 1;
    }

    if( do_list )
    {
        list_sprites(&cache, probe);
        tool_dat2_close(&cache);
        return 0;
    }

    table = RSCache_Dat2DiskTableId(cache.disk, RSCACHE_DAT2_TABLE_SPRITES);
    if( table != RSCACHE_DAT2_DISK_TABLE_ABSENT )
        rt = cache.disk->tables[table];

    for( int i = 0; i < sprite_count; i++ )
    {
        struct RSCache_Dat2SpritePack* pack;

        if( sprites[i].by_name[0] )
        {
            int hash = RSCache_ArchiveNameHashDat2(sprites[i].by_name);
            sprites[i].archive_id = -1;
            for( int a = 0; rt && a < rt->archive_count; a++ )
                if( rt->archives[a].identifier == hash )
                {
                    sprites[i].archive_id = rt->archives[a].index;
                    break;
                }
            if( sprites[i].archive_id < 0 )
            {
                fprintf(
                    stderr, "no sprite archive carries the name '%s'\n", sprites[i].by_name);
                return 1;
            }
        }

        pack = load_pack(&cache, sprites[i].archive_id);
        if( !pack )
        {
            fprintf(stderr, "archive %d does not decode as a sprite pack\n", sprites[i].archive_id);
            return 1;
        }
        sprites[i].argb = frame_to_argb(pack, sprites[i].frame, &sprites[i].w, &sprites[i].h);
        RSCache_Dat2SpritePackFree(pack);
        if( !sprites[i].argb )
        {
            fprintf(
                stderr, "archive %d frame %d has no pixels\n", sprites[i].archive_id,
                sprites[i].frame);
            return 1;
        }
        printf(
            "baked %-20s from archive %5d frame %d: %dx%d\n", sprites[i].symbol,
            sprites[i].archive_id, sprites[i].frame, sprites[i].w, sprites[i].h);
    }

    {
        FILE* out = fopen(out_path, "wb");
        char const* header_name = NULL;
        if( !out )
        {
            fprintf(stderr, "cannot write %s\n", out_path);
            return 1;
        }
        if( header_path )
        {
            char const* slash = strrchr(header_path, '/');
            header_name = slash ? slash + 1 : header_path;
        }
        emit_source(out, cache_dir, rev, prefix, header_name, sprites, sprite_count);
        fclose(out);
        printf("wrote %s\n", out_path);
    }

    if( header_path )
    {
        FILE* out = fopen(header_path, "wb");
        if( !out )
        {
            fprintf(stderr, "cannot write %s\n", header_path);
            return 1;
        }
        emit_header(out, cache_dir, rev, prefix, sprites, sprite_count);
        fclose(out);
        printf("wrote %s\n", header_path);
    }

    if( sheet_path )
        emit_sheet(sheet_path, sprites, sprite_count);

    for( int i = 0; i < sprite_count; i++ )
        free(sprites[i].argb);
    tool_dat2_close(&cache);
    return 0;
}
