/*
 * ev_icons — every obj's inventory icon, and the account site's other art,
 * rendered headless once per cache for the staff player editor
 * (src/websiteserver/website_site.c /admin/players/<name>).
 *
 *   ev_icons --rev osrs242 ../../cache.osrs242 \
 *            --names ../../OSRS-Content/osrs-content --out /tmp/admin-art
 *
 * The site is server-rendered HTML; an obj icon is a 3D model rasterized by
 * the client's own ToriDraw path, which no web server should carry. So the
 * icons are drawn here, the client's way (ev_build_obj_model + the client's
 * ToriDraw_SpriteNewFromObjIconRaster at the record's 2D camera, the bank
 * note's paper-plus-item composite from uitree_scene_bridge.c), into atlas
 * sheets, with an index the page searches. Written into <out>:
 *
 *   objs.json      {"cell":[36,32],"cols":32,"per_sheet":1024,"sheets":N,
 *                   "objs":[[id,"name","pack",flags,wearpos,icon],...]}
 *                  flags: 1 stackable, 2 noted, 4 members, 8 tradeable;
 *                  wearpos -1 for none; icon -1 for none, else the cell
 *                  (sheet icon/1024, row icon%1024/32, column icon%32);
 *                  variants 0, else [[count,icon],...]: the icon a stack of at
 *                  least that count draws as (coins become a pile)
 *   icons_<n>.png  sheet n, 32 x 32 cells of 36 x 32, transparent
 *   skill_<stat>.png  the stat's icon (staticons / staticons2), by stat id
 *   slot_<wearpos>.png the empty equipment slot's icon (wornicons)
 *   varps.json     {"varps":[[id,"name"],...]} from --names
 *   invs.json      {"invs":[[id,"name"],...]} from --names
 *
 * Objs with no name and no note link are skipped, as are bank placeholders:
 * nobody puts one in a slot by name.
 */

#include "ev_build.h"
#include "ev_config.h"
#include "ev_headless.h"
#include "ev_render.h"

#include "asset_access.h"
#include "rscache.h"
#include "tool_profile.h"
#include "toridraw.h"

#include "miniz.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ICON_W 36
#define ICON_H 32
#define SHEET_COLS 32
#define SHEET_ROWS 32
#define PER_SHEET (SHEET_COLS * SHEET_ROWS)
/* Consecutive absent ids that end the walk. The cache's ids are dense; a
 * gap this long is the end of them. */
#define OBJ_GAP_END 4000
#define NAME_TABLE_MAX 70000

/* The stat ids' icons: sprite archives staticons (197..214) and staticons2
 * (215..232), mapped by picture, in stat id order (pack/stat.pack). */
static int const k_stat_sprite[] = {
    197, /* 0 attack */
    199, /* 1 defence */
    198, /* 2 strength */
    203, /* 3 hitpoints */
    200, /* 4 ranged */
    201, /* 5 prayer */
    202, /* 6 magic */
    212, /* 7 cooking */
    214, /* 8 woodcutting */
    208, /* 9 fletching */
    211, /* 10 fishing */
    213, /* 11 firemaking */
    207, /* 12 crafting */
    210, /* 13 smithing */
    209, /* 14 mining */
    205, /* 15 herblore */
    204, /* 16 agility */
    206, /* 17 thieving */
    216, /* 18 slayer */
    217, /* 19 farming */
    215, /* 20 runecraft */
    220, /* 21 hunter */
    221, /* 22 construction */
    228, /* 23 sailing */
};

/* The equipment slots' empty icons (wornicons, 156..166), by wearpos. */
static int const k_slot_sprite[][2] = {
    { 0, 156 },  /* head */
    { 1, 157 },  /* cape */
    { 2, 158 },  /* neck */
    { 3, 159 },  /* weapon */
    { 4, 161 },  /* body */
    { 5, 162 },  /* shield */
    { 7, 163 },  /* legs */
    { 9, 164 },  /* hands */
    { 10, 165 }, /* feet */
    { 12, 160 }, /* ring */
    { 13, 166 }, /* ammo */
};

/* ---- output ------------------------------------------------------------- */

static int
write_png(
    char const* path,
    uint32_t const* argb,
    int w,
    int h)
{
    uint8_t* rgba = malloc((size_t)w * (size_t)h * 4);
    size_t len = 0;
    void* png;
    FILE* f;
    int ok;

    assert(rgba);
    for( int i = 0; i < w * h; i++ )
    {
        rgba[i * 4 + 0] = (uint8_t)(argb[i] >> 16);
        rgba[i * 4 + 1] = (uint8_t)(argb[i] >> 8);
        rgba[i * 4 + 2] = (uint8_t)argb[i];
        /* The raster's pixels are xRGB: no alpha lane, colour 0 is the
         * transparent key (as the client draws a sprite). A cache sprite's
         * decoded pixels do carry alpha, and keep it. */
        rgba[i * 4 + 3] = (argb[i] >> 24) ? (uint8_t)(argb[i] >> 24)
                                          : ((argb[i] & 0x00FFFFFFu) ? 255 : 0);
    }
    png = tdefl_write_image_to_png_file_in_memory_ex(rgba, w, h, 4, &len, 9, 0);
    free(rgba);
    assert(png);
    f = fopen(path, "wb");
    if( !f )
    {
        fprintf(stderr, "ev_icons: cannot write %s\n", path);
        mz_free(png);
        return 0;
    }
    ok = fwrite(png, 1, len, f) == len;
    ok = fclose(f) == 0 && ok;
    mz_free(png);
    return ok;
}

static void
json_string(
    FILE* f,
    char const* s)
{
    fputc('"', f);
    for( ; s && *s; s++ )
    {
        unsigned char ch = (unsigned char)*s;

        if( ch == '"' || ch == '\\' )
            fprintf(f, "\\%c", ch);
        else if( ch < 0x20 )
            fprintf(f, "\\u%04x", ch);
        else if( ch >= 0x80 )
            /* The cache's names are cp1252; the page wants UTF-8. Latin-1 is
             * its first 256 code points, which covers what names use. */
            fprintf(f, "%c%c", 0xC0 | (ch >> 6), 0x80 | (ch & 0x3F));
        else
            fputc(ch, f);
    }
    fputc('"', f);
}

/* ---- names from the content tree ---------------------------------------- */

/* `<id>=<name>` lines of a configs/all.<kind>.compack into `names` (each
 * malloc'd); the highest id + 1, or 0 when the file is absent. */
static int
read_compack(
    char const* content,
    char const* kind,
    char** names,
    int cap)
{
    char path[1024];
    char line[512];
    FILE* f;
    int top = 0;

    snprintf(path, sizeof(path), "%s/configs/all.%s.compack", content, kind);
    f = fopen(path, "r");
    if( !f )
    {
        fprintf(stderr, "ev_icons: no %s (names left out)\n", path);
        return 0;
    }
    while( fgets(line, sizeof(line), f) )
    {
        char* eq;
        int id;

        if( line[0] == '/' || !(eq = strchr(line, '=')) )
            continue;
        id = atoi(line);
        if( id < 0 || id >= cap )
            continue;
        line[strcspn(line, "\r\n")] = '\0';
        free(names[id]);
        names[id] = strdup(eq + 1);
        assert(names[id]);
        if( id + 1 > top )
            top = id + 1;
    }
    fclose(f);
    return top;
}

/* ---- sprites ------------------------------------------------------------ */

/* Frame 0 of sprite archive `archive` as ARGB into a new buffer; NULL when the
 * cache has none. */
static uint32_t*
sprite_argb(
    struct Tool_Dat2Cache* c,
    int archive,
    int* out_w,
    int* out_h)
{
    int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_SPRITES);
    struct Tool_Bytes raw = { 0 };
    struct RSCache_Dat2SpritePack* pack;
    struct RSCache_Dat2Sprite* s;
    int* pixels;
    uint32_t* out;
    int n;

    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return NULL;
    if( !tool_dat2_archive_bytes(c, table, archive, &raw) || raw.size <= 0 )
        return NULL;
    pack = RSCache_Dat2SpritePackNewDecode(raw.data, raw.size, RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
    tool_bytes_free(&raw);
    if( !pack || pack->count < 1 )
    {
        if( pack )
            RSCache_Dat2SpritePackFree(pack);
        return NULL;
    }
    s = &pack->sprites[0];
    pixels = RSCache_Dat2SpriteGetPixels(s, pack->palette, 0);
    n = s->crop_width * s->crop_height;
    out = calloc((size_t)(n > 0 ? n : 1), sizeof(*out));
    assert(out);
    for( int i = 0; pixels && i < n; i++ )
        out[i] = (uint32_t)pixels[i];
    *out_w = s->crop_width;
    *out_h = s->crop_height;
    free(pixels);
    RSCache_Dat2SpritePackFree(pack);
    return out;
}

static int
write_sprite(
    struct Tool_Dat2Cache* c,
    int archive,
    char const* path)
{
    int w = 0;
    int h = 0;
    uint32_t* argb = sprite_argb(c, archive, &w, &h);
    int ok;

    if( !argb )
    {
        fprintf(stderr, "ev_icons: sprite %d is not in this cache\n", archive);
        return 0;
    }
    ok = write_png(path, argb, w, h);
    free(argb);
    return ok;
}

/* ---- obj icons ---------------------------------------------------------- */

/* `obj` is record `id`: the decoder does not fill `_id`, so the id rides
 * beside it everywhere here. */
static struct ToriDraw_Sprite*
icon_of(
    struct Tool_Dat2Cache* c,
    int id,
    struct RSCache_Dat2ConfigObj const* obj,
    int zoom,
    int shadow)
{
    struct ToriDraw_Model* model;
    struct ToriDraw_Sprite* sprite;

    if( obj->inventory_model_id <= 0 )
        return NULL;
    model = ev_build_obj_model(c, id, EV_OBJ_MODEL_ITEM);
    if( !model )
        return NULL;
    sprite = ev_obj_icon_sprite(model, zoom, obj->xan2d, obj->yan2d, obj->zan2d,
                                obj->offset_x2d, obj->offset_y2d, shadow);
    ToriDraw_ModelFree(model);
    return sprite;
}

/* The obj a stack of `count` draws as (count_co / count_obj), else `id`. */
static int
count_variant(
    int id,
    struct RSCache_Dat2ConfigObj const* obj,
    int count)
{
    int pick = -1;

    for( int i = 0; i < 10; i++ )
        if( obj->count_co[i] > 0 && count >= obj->count_co[i] && obj->count_obj[i] > 0 )
            pick = obj->count_obj[i];
    return pick > 0 ? pick : id;
}

/* A bank note: the template's paper with the base item (its count-10 pile,
 * zoom * 1.5, a black edge, no shadow) laid over it -- uitree_scene_bridge.c's
 * cert branch. NULL when either half does not draw. */
static struct ToriDraw_Sprite*
note_icon(
    struct Tool_Dat2Cache* c,
    struct RSCache_Dat2ConfigObj const* note)
{
    struct RSCache_Dat2ConfigObj* tmpl = ev_obj_load(c, note->noted_template);
    struct RSCache_Dat2ConfigObj* linked = ev_obj_load(c, note->noted_id);
    struct RSCache_Dat2ConfigObj* base = NULL;
    struct ToriDraw_Sprite* paper = NULL;
    struct ToriDraw_Sprite* item = NULL;
    int base_id = note->noted_id;

    if( linked )
    {
        base_id = count_variant(note->noted_id, linked, 10);
        base = base_id == note->noted_id ? linked : ev_obj_load(c, base_id);
        if( !base )
        {
            base = linked;
            base_id = note->noted_id;
        }
    }
    if( tmpl && base )
    {
        item = icon_of(c, base_id, base, (base->zoom2d > 0 ? base->zoom2d : 2000) * 3 / 2, 0);
        paper = icon_of(c, note->noted_template, tmpl, tmpl->zoom2d, 1);
    }
    if( item && paper )
    {
        int ow = 0;
        int oh = 0;
        uint32_t* edged =
            ToriDraw_SpriteNewGraphicOutline(item->pixels_argb, item->width, item->height, 1, &ow, &oh);

        if( edged && ow == paper->width && oh == paper->height )
            for( int i = 0; i < ow * oh; i++ )
                if( edged[i] & 0x00FFFFFFu )
                    paper->pixels_argb[i] = edged[i];
        TORIDRAW_FREE(edged);
    }
    else if( paper )
    {
        ToriDraw_SpriteFree(paper);
        paper = NULL;
    }
    if( item )
        ToriDraw_SpriteFree(item);
    if( base && base != linked )
        RSCache_Dat2ConfigObjFree(base);
    if( linked )
        RSCache_Dat2ConfigObjFree(linked);
    if( tmpl )
        RSCache_Dat2ConfigObjFree(tmpl);
    return paper;
}

/* The display name: the record's, or for a note its base item's. Caller frees. */
static char*
obj_name(
    struct Tool_Dat2Cache* c,
    struct RSCache_Dat2ConfigObj const* obj)
{
    if( obj->noted_template >= 0 && obj->noted_id >= 0 )
    {
        struct RSCache_Dat2ConfigObj* base = ev_obj_load(c, obj->noted_id);
        char* name = NULL;

        if( base && base->name && strcmp(base->name, "null") != 0 )
            name = strdup(base->name);
        if( base )
            RSCache_Dat2ConfigObjFree(base);
        return name;
    }
    if( obj->name && obj->name[0] && strcmp(obj->name, "null") != 0 )
        return strdup(obj->name);
    return NULL;
}

/* ---- the atlas ---------------------------------------------------------- */

struct Atlas
{
    char const* out_dir;
    uint32_t* sheet;
    int icon;
    int sheet_index;
    int ok;
};

static void
atlas_flush(struct Atlas* a)
{
    char path[1200];

    snprintf(path, sizeof(path), "%s/icons_%d.png", a->out_dir, a->sheet_index++);
    a->ok &= write_png(path, a->sheet, SHEET_COLS * ICON_W, SHEET_ROWS * ICON_H);
    memset(a->sheet, 0, (size_t)PER_SHEET * ICON_W * ICON_H * sizeof(*a->sheet));
}

/* `sprite` into the next cell; the cell's index, or -1 for no icon. */
static int
atlas_put(
    struct Atlas* a,
    struct ToriDraw_Sprite const* sprite)
{
    int at;
    int x0;
    int y0;

    if( !sprite || sprite->width != ICON_W || sprite->height != ICON_H )
        return -1;
    at = a->icon % PER_SHEET;
    x0 = (at % SHEET_COLS) * ICON_W;
    y0 = (at / SHEET_COLS) * ICON_H;
    for( int y = 0; y < ICON_H; y++ )
        memcpy(&a->sheet[(size_t)(y0 + y) * SHEET_COLS * ICON_W + x0], &sprite->pixels_argb[y * ICON_W],
               ICON_W * sizeof(uint32_t));
    a->icon++;
    if( a->icon % PER_SHEET == 0 )
        atlas_flush(a);
    return a->icon - 1;
}

static void
usage(char const* prog)
{
    fprintf(stderr,
            "usage: %s --rev <rev> <cache dir> --out <dir> [--names <content dir>]\n"
            "  every obj's inventory icon as atlas sheets + objs.json, the skill and\n"
            "  equipment-slot icons, and (with --names) the varp names\n",
            prog);
}

int
main(
    int argc,
    char** argv)
{
    char const* rev_name = NULL;
    char const* cache_dir = NULL;
    char const* out_dir = NULL;
    char const* content = NULL;
    char path[1200];
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    char** pack_names;
    struct Atlas atlas;
    FILE* objs;
    int listed = 0;
    int gap = 0;
    int ok = 1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev_name = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_dir = argv[++i];
        else if( strcmp(argv[i], "--names") == 0 && i + 1 < argc )
            content = argv[++i];
        else if( argv[i][0] != '-' && !cache_dir )
            cache_dir = argv[i];
        else
        {
            usage(argv[0]);
            return 2;
        }
    }
    if( !rev_name || !cache_dir || !out_dir )
    {
        usage(argv[0]);
        return 2;
    }
    if( !tool_resolve_profile(rev_name, NULL, NULL, NULL, NULL, &profile) )
    {
        fprintf(stderr, "ev_icons: unknown revision '%s'\n", rev_name);
        return 1;
    }
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "ev_icons: cannot open cache at %s\n", cache_dir);
        return 1;
    }
    mkdir(out_dir, 0755);
    ev_config_memo(1);
    ToriDraw_Init();
    ev_init();
    ev_build_set_texture_flatten(ev_headless_flatten_texture, &cache);
    ev_headless_install_textures(&cache);

    /* The small art first: a missing sprite is a bad mapping, said loudly. */
    for( int s = 0; s < (int)(sizeof(k_stat_sprite) / sizeof(k_stat_sprite[0])); s++ )
    {
        snprintf(path, sizeof(path), "%s/skill_%d.png", out_dir, s);
        ok &= write_sprite(&cache, k_stat_sprite[s], path);
    }
    for( int s = 0; s < (int)(sizeof(k_slot_sprite) / sizeof(k_slot_sprite[0])); s++ )
    {
        snprintf(path, sizeof(path), "%s/slot_%d.png", out_dir, k_slot_sprite[s][0]);
        ok &= write_sprite(&cache, k_slot_sprite[s][1], path);
    }

    pack_names = calloc(NAME_TABLE_MAX, sizeof(*pack_names));
    assert(pack_names);
    if( content )
    {
        char** varps = calloc(NAME_TABLE_MAX, sizeof(*varps));
        int top;
        FILE* vf;

        assert(varps);
        top = read_compack(content, "varp", varps, NAME_TABLE_MAX);
        snprintf(path, sizeof(path), "%s/varps.json", out_dir);
        vf = fopen(path, "w");
        assert(vf);
        fprintf(vf, "{\"varps\":[");
        for( int id = 0, first = 1; id < top; id++ )
        {
            if( !varps[id] )
                continue;
            fprintf(vf, "%s[%d,", first ? "" : ",", id);
            json_string(vf, varps[id]);
            fputc(']', vf);
            first = 0;
            free(varps[id]);
        }
        fprintf(vf, "]}\n");
        ok &= fclose(vf) == 0;
        free(varps);
        /* The containers' names, for the page's other-container sections. */
        {
            char** invs = calloc(NAME_TABLE_MAX, sizeof(*invs));
            int inv_top;
            FILE* inf;

            assert(invs);
            inv_top = read_compack(content, "inv", invs, NAME_TABLE_MAX);
            snprintf(path, sizeof(path), "%s/invs.json", out_dir);
            inf = fopen(path, "w");
            assert(inf);
            fprintf(inf, "{\"invs\":[");
            for( int id = 0, first = 1; id < inv_top; id++ )
            {
                if( !invs[id] )
                    continue;
                fprintf(inf, "%s[%d,", first ? "" : ",", id);
                json_string(inf, invs[id]);
                fputc(']', inf);
                first = 0;
                free(invs[id]);
            }
            fprintf(inf, "]}\n");
            ok &= fclose(inf) == 0;
            free(invs);
        }
        read_compack(content, "obj", pack_names, NAME_TABLE_MAX);
    }

    memset(&atlas, 0, sizeof(atlas));
    atlas.out_dir = out_dir;
    atlas.ok = 1;
    atlas.sheet = calloc((size_t)PER_SHEET * ICON_W * ICON_H, sizeof(*atlas.sheet));
    assert(atlas.sheet);
    snprintf(path, sizeof(path), "%s/objs.json", out_dir);
    objs = fopen(path, "w");
    assert(objs);
    fprintf(objs, "{\"cell\":[%d,%d],\"cols\":%d,\"per_sheet\":%d,\"objs\":[", ICON_W, ICON_H,
            SHEET_COLS, PER_SHEET);

    for( int id = 0; gap < OBJ_GAP_END; id++ )
    {
        struct RSCache_Dat2ConfigObj* obj = ev_obj_load(&cache, id);
        struct ToriDraw_Sprite* sprite = NULL;
        char* name;
        int noted;
        int flags;
        int cell = -1;

        if( !obj )
        {
            gap++;
            continue;
        }
        gap = 0;
        noted = obj->noted_template >= 0 && obj->noted_id >= 0;
        name = obj->placeholder_template_id >= 0 ? NULL : obj_name(&cache, obj);
        if( !name )
        {
            RSCache_Dat2ConfigObjFree(obj);
            continue;
        }
        sprite = noted ? note_icon(&cache, obj) : icon_of(&cache, id, obj, obj->zoom2d, 1);
        cell = atlas_put(&atlas, sprite);
        if( sprite )
            ToriDraw_SpriteFree(sprite);

        flags = (obj->stacking_behaviour == 1 || noted ? 1 : 0) | (noted ? 2 : 0) |
                (obj->is_members ? 4 : 0) | (obj->tradeable ? 8 : 0);
        fprintf(objs, "%s[%d,", listed ? "," : "", id);
        json_string(objs, name);
        fputc(',', objs);
        json_string(objs, id < NAME_TABLE_MAX && pack_names[id] ? pack_names[id] : "");
        fprintf(objs, ",%d,%d,%d", flags, noted ? -1 : obj->wearpos_1, cell);
        /* The stack's looks: count thresholds and the icon cell of the obj
         * each draws as (count_co / count_obj: coins become a pile), else 0.
         * Those objs are mostly unnamed, so not listed: drawn here. */
        {
            int any = 0;

            for( int v = 0; v < 10 && !noted; v++ )
            {
                struct RSCache_Dat2ConfigObj* variant;
                int variant_cell = -1;

                if( obj->count_co[v] <= 0 || obj->count_obj[v] <= 0 )
                    continue;
                variant = ev_obj_load(&cache, obj->count_obj[v]);
                if( variant )
                {
                    struct ToriDraw_Sprite* vs =
                        icon_of(&cache, obj->count_obj[v], variant, variant->zoom2d, 1);

                    variant_cell = atlas_put(&atlas, vs);
                    if( vs )
                        ToriDraw_SpriteFree(vs);
                    RSCache_Dat2ConfigObjFree(variant);
                }
                if( variant_cell < 0 )
                    continue;
                fprintf(objs, "%s[%d,%d]", any ? "," : ",[", obj->count_co[v], variant_cell);
                any = 1;
            }
            fprintf(objs, "%s]", any ? "]" : ",0");
        }
        listed++;
        free(name);
        RSCache_Dat2ConfigObjFree(obj);
        if( listed % 2000 == 0 )
            fprintf(stderr, "ev_icons: %d objs, %d icons\n", listed, atlas.icon);
    }
    if( atlas.icon % PER_SHEET != 0 )
        atlas_flush(&atlas);
    ok &= atlas.ok;
    fprintf(objs, "],\"sheets\":%d}\n", atlas.sheet_index);
    ok &= fclose(objs) == 0;
    free(atlas.sheet);
    for( int i = 0; i < NAME_TABLE_MAX; i++ )
        free(pack_names[i]);
    free(pack_names);
    fprintf(stderr, "ev_icons: %d objs listed, %d icons on %d sheet(s) in %s\n", listed, atlas.icon,
            atlas.sheet_index, out_dir);
    ev_config_memo(0);
    tool_dat2_close(&cache);
    return ok ? 0 : 1;
}
