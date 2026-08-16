/*
 * ev_hd_sheet — sweep HD compositing hypotheses in one run, as one image.
 *
 * ## Why this exists
 *
 * Matching the HD kernels to the original is a comparison problem, not a
 * derivation: the rules are being recovered by looking at output. Doing that one
 * render per rebuild is the slow way, and worse, it invites judging two images
 * seen minutes apart — which is exactly how a change that makes things slightly
 * worse gets kept.
 *
 * So this renders every variant in a single process, into one contact sheet,
 * side by side at identical camera and framing. A hypothesis that is wrong is
 * obvious next to its neighbours in a way it never is alone.
 *
 * It also prints per-variant metrics, because "looks better" and "is closer"
 * diverge: a render can gain saturation and lose contrast at the same time and
 * read as an improvement.
 *
 *   mean rgb      the average of drawn pixels
 *   chroma        mean (max channel - min channel); saturation
 *   contrast      standard deviation of luminance; the official render has
 *                 strong black shadows, so a flat, muddy result scores low
 *   dark / bright fraction of drawn pixels below 40 / above 200 luminance
 *
 * Build and run:
 *   make -C tools/entity_viewer ev_hd_sheet
 *   tools/entity_viewer/ev_hd_sheet cache.rs727_preeoc rs727 2745 --out /tmp/sheet.bmp
 *
 * Options:
 *   --tile N     tile pixel size (default 256)
 *   --yaw N      camera yaw (default 300)
 *   --pitch N    camera pitch (default 200)
 *   --one            render only the shipped default, for a quick before/after
 *   --no-priorities  draw as if the model carried no face priorities
 */

#include "ev_build.h"
#include "ev_render.h"
#include "ev_textures.h"
#include "ev_wire.h"

#include "asset_access.h"
#include "tool_profile.h"
#include "toridraw.h"
#include "toridraw_render_hd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static struct EV_TextureSet g_set;

static int
have_texture(int id, void* user)
{
    (void)user;
    return ev_textures_get(&g_set, id) != NULL;
}

/* ---- the variants under test --------------------------------------------- */

/*
 * Each row is one hypothesis about the compositing rule. The first is the
 * shipped default and is always rendered first, so every other tile is read as
 * a delta from what the tree currently does.
 */
struct variant
{
    const char* name;
    int tint_lightness;  /* <0 = the face's authored lightness */
    int tint_scale;      /* percent */
    int texture_neutral; /* texel value that leaves the colour unchanged */
    int tint_saturation; /* percent of the face colour's saturation kept */
};

static const struct variant g_variants[] = {
    { "SHIPPED (sat 100)", -1, 100, 0, 100 },
    { "sat 90", -1, 100, 0, 90 },
    { "sat 80", -1, 100, 0, 80 },
    { "sat 70", -1, 100, 0, 70 },
    { "sat 60", -1, 100, 0, 60 },
    { "sat 50", -1, 100, 0, 50 },
    { "sat 40", -1, 100, 0, 40 },
    { "sat 70, 120%", -1, 120, 0, 70 },
    { "OLD default", TORIDRAW_HD_MODULATE_LIGHTNESS, 100, 255, 100 },
};
#define VARIANT_COUNT ((int)(sizeof(g_variants) / sizeof(g_variants[0])))

/* ---- metrics -------------------------------------------------------------- */

struct metrics
{
    int drawn;
    int r, g, b;
    double chroma;
    double contrast;
    double dark;
    double bright;
    /* Fraction of drawn pixels with a channel pinned at 255.
     *
     * Clipping is invisible in the mean and destroys detail: once a channel
     * saturates, the texture's variation in it is gone, and what survives is
     * whatever the other channels do. On a red surface that reads as contour
     * banding, which is easy to misread as a uv or mapping fault. */
    double clipped;
    /*
     * Directional gradient anisotropy over drawn pixels.
     *
     * Streaking is texture detail smeared along one direction: strong gradient
     * across the smear, almost none along it. Averaging |dx| and |dy| per pixel
     * and taking the ratio detects that without needing to see the image, which
     * is the whole problem with chasing a visual artefact from metrics. Near 1
     * is isotropic detail; large means the surface varies in one direction only.
     */
    double streak;
};

/* The viewer's background, which is not part of the subject. */
static int
is_background(const unsigned char* p)
{
    return p[0] == 0x14 && p[1] == 0x18 && p[2] == 0x21;
}

static struct metrics
measure(const unsigned char* rgba, int w, int h)
{
    struct metrics m;
    memset(&m, 0, sizeof(m));

    long long sr = 0, sg = 0, sb = 0, sc = 0;
    double lum_sum = 0, lum_sq = 0;
    int dark = 0, bright = 0, n = 0, clipped = 0;

    for( int i = 0; i < w * h; i++ )
    {
        const unsigned char* p = rgba + i * 4;
        if( is_background(p) )
            continue;
        int r = p[0], g = p[1], b = p[2];
        int hi = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
        double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;

        sr += r;
        sg += g;
        sb += b;
        sc += hi - lo;
        lum_sum += lum;
        lum_sq += lum * lum;
        if( lum < 40 )
            dark++;
        if( lum > 200 )
            bright++;
        if( r == 255 || g == 255 || b == 255 )
            clipped++;
        n++;
    }
    if( !n )
        return m;

    m.drawn = n;
    m.r = (int)(sr / n);
    m.g = (int)(sg / n);
    m.b = (int)(sb / n);
    m.chroma = (double)sc / n;
    double mean = lum_sum / n;
    m.contrast = lum_sq / n - mean * mean;
    m.contrast = m.contrast > 0 ? __builtin_sqrt(m.contrast) : 0;
    m.dark = 100.0 * dark / n;
    m.bright = 100.0 * bright / n;
    m.clipped = 100.0 * clipped / n;

    {
        double gx = 0, gy = 0;
        int gn = 0;
        for( int y = 1; y < h - 1; y++ )
            for( int x = 1; x < w - 1; x++ )
            {
                const unsigned char* c = rgba + (y * w + x) * 4;
                const unsigned char* rx = rgba + (y * w + x + 1) * 4;
                const unsigned char* ry = rgba + ((y + 1) * w + x) * 4;
                if( is_background(c) || is_background(rx) || is_background(ry) )
                    continue;
                gx += abs((int)c[0] - rx[0]) + abs((int)c[1] - rx[1]) + abs((int)c[2] - rx[2]);
                gy += abs((int)c[0] - ry[0]) + abs((int)c[1] - ry[1]) + abs((int)c[2] - ry[2]);
                gn++;
            }
        if( gn )
        {
            double a = gx / gn, b = gy / gn;
            double hi = a > b ? a : b, lo = a > b ? b : a;
            m.streak = lo > 1e-6 ? hi / lo : 0.0;
        }
    }
    return m;
}

/* ---- output --------------------------------------------------------------- */

static void
write_bmp(const char* path, const unsigned char* rgb, int w, int h)
{
    FILE* f = fopen(path, "wb");
    if( !f )
    {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    int row = w * 3, pad = (4 - (row % 4)) % 4, data = (row + pad) * h;
    unsigned char hdr[54] = { 'B', 'M' };
    int fsz = 54 + data, off = 54, ih = 40;
    memcpy(hdr + 2, &fsz, 4);
    memcpy(hdr + 10, &off, 4);
    memcpy(hdr + 14, &ih, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    memcpy(hdr + 34, &data, 4);
    fwrite(hdr, 1, 54, f);
    for( int y = h - 1; y >= 0; y-- )
    {
        for( int x = 0; x < w; x++ )
        {
            const unsigned char* p = rgb + (y * w + x) * 3;
            unsigned char bgr[3] = { p[2], p[1], p[0] };
            fwrite(bgr, 1, 3, f);
        }
        unsigned char z[3] = { 0, 0, 0 };
        fwrite(z, 1, (size_t)pad, f);
    }
    fclose(f);
}

/* An EVT1 blob for every texture the model names, exactly as the server builds
 * one — so the harness exercises the same install path the browser does. */
static uint8_t*
build_blob(const struct ToriDraw_Model* m, size_t* out_len)
{
    int ids[256], n = 0;
    for( int f = 0; f < m->face_count; f++ )
    {
        int id = m->face_textures ? m->face_textures[f] : -1;
        int dup = 0;
        if( id < 0 )
            continue;
        for( int i = 0; i < n; i++ )
            if( ids[i] == id )
                dup = 1;
        if( !dup && n < 256 )
            ids[n++] = id;
    }

    size_t cap = 8;
    for( int i = 0; i < n; i++ )
    {
        const struct EV_Texture* t = ev_textures_get(&g_set, ids[i]);
        if( t )
            cap += 12 + (size_t)t->size * t->size * 4;
    }
    uint8_t* b = malloc(cap);
    if( !b )
        return NULL;

    size_t at = 0;
    uint32_t magic = 0x31545645u, count = 0;
    memcpy(b + at, &magic, 4);
    at += 4;
    size_t count_at = at;
    at += 4;
    for( int i = 0; i < n; i++ )
    {
        const struct EV_Texture* t = ev_textures_get(&g_set, ids[i]);
        if( !t )
            continue;
        uint32_t id32 = (uint32_t)ids[i];
        memcpy(b + at, &id32, 4);
        at += 4;
        b[at++] = (uint8_t)(t->size & 0xFF);
        b[at++] = (uint8_t)((t->size >> 8) & 0xFF);
        b[at++] = (uint8_t)(t->alpha_mode >= 0 && t->alpha_mode <= 2 ? t->alpha_mode : 0);
        b[at++] = (uint8_t)((t->repeat_s ? 0 : 1) | (t->repeat_t ? 0 : 2) | (t->modulate ? 4 : 0));
        b[at++] = (uint8_t)(t->mean_luma < 1 ? 1 : t->mean_luma);
        b[at++] = 0;
        b[at++] = 0;
        b[at++] = 0;
        for( int p = 0; p < t->size * t->size; p++ )
        {
            uint32_t v = (uint32_t)t->texels[p];
            memcpy(b + at, &v, 4);
            at += 4;
        }
        count++;
    }
    memcpy(b + count_at, &count, 4);
    *out_len = at;
    return b;
}

int
main(int argc, char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : "cache.rs727_preeoc";
    const char* rev = argc > 2 ? argv[2] : "rs727";
    int npc_id = argc > 3 ? atoi(argv[3]) : 2745;
    const char* out = "/tmp/ev_hd_sheet.bmp";
    int tile = 256, yaw = 300, pitch = 200, only_default = 0, no_priorities = 0;
    /* Framing multiplier over the model's own height. 1.6 fills the tile; the
     * sweep wants the whole subject, so it starts wider. */
    double frame = 2.6;

    for( int i = 4; i < argc; i++ )
    {
        if( !strcmp(argv[i], "--out") && i + 1 < argc )
            out = argv[++i];
        else if( !strcmp(argv[i], "--tile") && i + 1 < argc )
            tile = atoi(argv[++i]);
        else if( !strcmp(argv[i], "--yaw") && i + 1 < argc )
            yaw = atoi(argv[++i]);
        else if( !strcmp(argv[i], "--pitch") && i + 1 < argc )
            pitch = atoi(argv[++i]);
        else if( !strcmp(argv[i], "--frame") && i + 1 < argc )
            frame = atof(argv[++i]);
        else if( !strcmp(argv[i], "--no-priorities") )
            no_priorities = 1;
        else if( !strcmp(argv[i], "--one") )
            only_default = 1;
    }

    /* Before any model is built: lighting and the bounds cylinder read the trig
     * tables, and a model built without them is culled at every camera. */
    ToriDraw_Init();

    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
    {
        fprintf(stderr, "unknown revision %s\n", rev);
        return 1;
    }
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "cannot open %s\n", cache_dir);
        return 1;
    }
    if( !ev_textures_load(&cache, &g_set) )
        fprintf(stderr, "warning: textures did not load\n");

    ev_build_set_texture_available(have_texture, NULL);

    struct ToriDraw_ModelHD* hd = ev_build_npc_model_hd(&cache, npc_id);
    struct ToriDraw_Model* model = hd ? &hd->base : ev_build_npc_model(&cache, npc_id);
    if( !model )
    {
        fprintf(stderr, "npc %d has no model\n", npc_id);
        return 1;
    }

    struct EV_WireBuf wb = { 0 };
    if( hd )
        ev_wire_write_model_hd(&wb, hd);
    else
        ev_wire_write_model(&wb, model);
    size_t blob_len = 0;
    uint8_t* blob = build_blob(model, &blob_len);

    ev_init();
    ev_set_ignore_priorities(no_priorities);
    int faces = hd ? ev_set_model_hd(wb.data, (int)wb.len) : ev_set_model(wb.data, (int)wb.len);
    int installed = blob ? ev_set_textures(blob, (int)blob_len) : 0;

    int zoom = (int)(ev_model_height() * frame);
    if( zoom < 260 )
        zoom = 260;

    printf("%s npc %d: %s, %d faces, %d textures, zoom %d\n",
           cache_dir, npc_id, hd ? "HD" : "plain", faces, installed, zoom);

    /*
     * The priority histogram.
     *
     * Face priority is 0..15 and is a DRAW-ORDER band: the sorter emits all of
     * one band before the next. A model using it as intended spreads faces over
     * a handful of low bands. A model that packs some other meaning in here —
     * which is the open question for the HD ones — tends to look different:
     * every face on one non-zero band, or values spread across the whole range
     * with no relation to what overlaps what.
     */
    {
        int hist[16] = { 0 }, none = 0;
        for( int f = 0; f < model->face_count; f++ )
        {
            if( !model->face_priorities )
            {
                none++;
                continue;
            }
            /* Two priorities per byte, low nibble first — see
             * torirs_set_face_priority. Indexing this array by face reads the
             * wrong faces' values and invents a distribution. */
            int pr = (f & 1) ? (model->face_priorities[f >> 1] >> 4)
                             : (model->face_priorities[f >> 1] & 0x0F);
            hist[pr]++;
        }
        printf("  priorities: model_priority=%d", model->model_priority);
        if( none )
            printf(" (no per-face array)");
        else
        {
            printf(" bands used:");
            for( int i = 0; i < 16; i++ )
                if( hist[i] )
                    printf(" %d=%d", i, hist[i]);
        }
        printf("\n");
    }
    printf("%-30s %-16s %7s %9s %7s %8s %8s %8s\n",
           "variant", "mean rgb", "chroma", "contrast", "dark%", "bright%", "clip%", "streak");

    /*
     * EV_FACE_AUDIT=1 — why each face is not drawn, grouped by texture.
     *
     * "A detail is missing" has two very different causes: the face is skipped
     * before it reaches a kernel, or it is drawn and then painted over by
     * something sorted after it. The counters below separate them; anything
     * that is neither skipped nor missing a material was drawn, so if it is not
     * visible the sort buried it.
     */
    if( getenv("EV_FACE_AUDIT") )
    {
        printf("  face audit (per texture id):\n");
        int ids[64], n_ids = 0;
        for( int f = 0; f < model->face_count; f++ )
        {
            int t = model->face_textures ? model->face_textures[f] : -1;
            int dup = 0;
            for( int i = 0; i < n_ids; i++ )
                if( ids[i] == t )
                    dup = 1;
            if( !dup && n_ids < 64 )
                ids[n_ids++] = t;
        }
        for( int i = 0; i < n_ids; i++ )
        {
            int total = 0, hidden_c = 0, info2 = 0, no_colors = 0, alpha0 = 0;
            int pr_hist[16] = { 0 };
            for( int f = 0; f < model->face_count; f++ )
            {
                int t = model->face_textures ? model->face_textures[f] : -1;
                if( t != ids[i] )
                    continue;
                total++;
                int raw = model->face_infos ? model->face_infos[f] : 0;
                if( raw == 2 || raw < 0 || raw > 3 )
                    info2++;
                if( !model->face_colors_a || !model->face_colors_c )
                    no_colors++;
                else if( model->face_colors_c[f] == -2 )
                    hidden_c++;
                if( model->face_alphas && (0xFF - (model->face_alphas[f] & 0xFF)) <= 1 )
                    alpha0++;
                if( model->face_priorities )
                {
                    int pr = (f & 1) ? (model->face_priorities[f >> 1] >> 4)
                                     : (model->face_priorities[f >> 1] & 0x0F);
                    pr_hist[pr]++;
                }
            }
            printf("    tex %-5d faces=%-5d skipped[info=%d hiddenC=%d nocol=%d alpha0=%d] pri:",
                   ids[i], total, info2, hidden_c, no_colors, alpha0);
            for( int b = 0; b < 16; b++ )
                if( pr_hist[b] )
                    printf(" %d=%d", b, pr_hist[b]);
            printf("\n");
        }
    }

    int n = only_default ? 1 : VARIANT_COUNT;
    int cols = n > 3 ? 3 : n;
    int rows = (n + cols - 1) / cols;
    int sheet_w = cols * tile, sheet_h = rows * tile;
    unsigned char* sheet = calloc((size_t)sheet_w * sheet_h * 3, 1);
    if( !sheet )
        return 1;

    for( int v = 0; v < n; v++ )
    {
        struct ToriDraw_HDTuning tuning;
        tuning.tint_lightness = g_variants[v].tint_lightness;
        tuning.tint_scale = g_variants[v].tint_scale;
        tuning.texture_neutral = g_variants[v].texture_neutral;
        tuning.tint_saturation = g_variants[v].tint_saturation;
        ToriDraw_HDSetTuning(&tuning);

        const unsigned char* rgba = ev_render(tile, tile, yaw, pitch, zoom, 0);
        if( !rgba )
        {
            fprintf(stderr, "render failed for variant %d\n", v);
            continue;
        }

        struct metrics m = measure(rgba, tile, tile);
        printf("%-30s (%3d,%3d,%3d)   %7.1f %9.1f %7.1f %8.1f %8.1f %8.2f\n",
               g_variants[v].name, m.r, m.g, m.b, m.chroma, m.contrast, m.dark,
               m.bright, m.clipped, m.streak);

        int cx = (v % cols) * tile, cy = (v / cols) * tile;
        for( int y = 0; y < tile; y++ )
            for( int x = 0; x < tile; x++ )
            {
                const unsigned char* p = rgba + (y * tile + x) * 4;
                unsigned char* d = sheet + ((cy + y) * sheet_w + (cx + x)) * 3;
                d[0] = p[0];
                d[1] = p[1];
                d[2] = p[2];
            }
    }
    ToriDraw_HDSetTuning(NULL);

    write_bmp(out, sheet, sheet_w, sheet_h);
    printf("wrote %s (%dx%d, %d tiles, row-major in the order listed)\n",
           out, sheet_w, sheet_h, n);

    free(sheet);
    free(blob);
    ev_wire_free(&wb);
    ev_textures_free(&g_set);
    tool_dat2_close(&cache);
    return 0;
}
