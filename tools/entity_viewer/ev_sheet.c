/*
 * ev_sheet — render an entity's animation to PNG frames, headless.
 *
 * The viewer proper is a browser page over ev_server, and ev_swing renders as
 * a side effect of measuring a weapon arc. Neither is usable as "give me this
 * npc playing this sequence, as frames, with the timing". This does that and
 * nothing else: no server, no wasm, no page.
 *
 *   ev_sheet --rev osrs239 ../../cache.osrs239 --npc 3127 --seq 2652 \
 *            --fit 320 --out /tmp/jad
 *
 * ## Real alpha, not a colour key
 *
 * ev_render paints a background and then stamps every pixel opaque, so its
 * alpha carries no information. Keying the background out afterwards means
 * guessing which pixels are background from their colour, and that deletes
 * model pixels which happen to resemble it — for a dark model on the viewer's
 * dark panel, most of the silhouette.
 *
 * So every frame is rendered TWICE, against black and against white. A pixel
 * the model covers is identical in both. One it does not covers differs by the
 * full 255. A partly-covered pixel lands in between, and because
 *
 *     c_white - c_black == (1 - alpha) * 255
 *
 * its coverage is recoverable exactly rather than merely detectable. The model
 * colour then un-premultiplies out of the black pass. No thresholds anywhere.
 *
 * ## Timing
 *
 * Frame durations come from the sequence itself (ev_frame_delay, in 20ms
 * client ticks) and are written to frames.txt. A sequence holds most of its
 * poses for several ticks each, so playing frames at a constant rate is wrong
 * — a chop that lingers at the top of the swing loses the linger.
 *
 * ## Fitting
 *
 * --fit sizes the subject rather than the canvas. The camera is fitted to the
 * UNION of every frame's coverage, so an arm raised on one frame does not get
 * clipped and the subject does not drift between frames. All frames are then
 * cropped to that one box, which is what a sprite sheet and a GIF both want.
 */

#include "ev_build.h"
#include "ev_config.h"
#include "ev_player.h"
#include "ev_render.h"
#include "ev_textures.h"
#include "ev_wire.h"

#include "asset_access.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "rscache.h"
#include "tool_profile.h"
#include "toridraw.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EV_SHEET_MAX_FRAMES 512
#define EV_SHEET_MAX_WORN 16

/* ev_render allocates its canvas up to EV_MAX_DIM (2048), which is private to
 * that file; keep the tool inside it. */
#define EV_SHEET_MAX_SIDE 2048

/* ------------------------------------------------------------------ PNG out */

static unsigned int g_crc_table[256];
static int g_crc_ready = 0;

static void
crc_init(void)
{
    for( unsigned int n = 0; n < 256; n++ )
    {
        unsigned int c = n;
        for( int k = 0; k < 8; k++ )
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[n] = c;
    }
    g_crc_ready = 1;
}

static unsigned int
crc_update(
    unsigned int crc,
    const unsigned char* buf,
    size_t len)
{
    if( !g_crc_ready )
        crc_init();
    for( size_t i = 0; i < len; i++ )
        crc = g_crc_table[(crc ^ buf[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

static unsigned int
adler32(
    const unsigned char* data,
    size_t len)
{
    unsigned int a = 1, b = 0;
    for( size_t i = 0; i < len; i++ )
    {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void
put_be32(
    unsigned char* p,
    unsigned int v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int
png_chunk(
    FILE* f,
    const char* type,
    const unsigned char* data,
    size_t len)
{
    unsigned char hdr[8];
    put_be32(hdr, (unsigned int)len);
    memcpy(hdr + 4, type, 4);
    if( fwrite(hdr, 1, 8, f) != 8 )
        return 0;
    if( len && fwrite(data, 1, len, f) != len )
        return 0;

    unsigned int crc = crc_update(0xFFFFFFFFu, (const unsigned char*)type, 4);
    if( len )
        crc = crc_update(crc, data, len);
    crc ^= 0xFFFFFFFFu;
    put_be32(hdr, crc);
    return fwrite(hdr, 1, 4, f) == 4;
}

/*
 * RGBA8 PNG, deflate "stored" blocks.
 *
 * Uncompressed on purpose: these are intermediates a pixel-art pass consumes
 * immediately, and a stored stream needs no zlib on the link line. It is a
 * valid PNG, just a large one.
 */
static int
png_write_rgba(
    const char* path,
    const unsigned char* rgba,
    int w,
    int h)
{
    assert(path);
    assert(rgba);

    size_t stride = (size_t)w * 4u + 1u;
    size_t rawlen = stride * (size_t)h;
    unsigned char* raw = (unsigned char*)malloc(rawlen);
    if( !raw )
        return 0;
    for( int y = 0; y < h; y++ )
    {
        raw[stride * (size_t)y] = 0; /* filter: none */
        memcpy(raw + stride * (size_t)y + 1, rgba + (size_t)y * (size_t)w * 4u,
               (size_t)w * 4u);
    }

    size_t zcap = 2 + rawlen + 5 * (rawlen / 65535u + 1u) + 4;
    unsigned char* z = (unsigned char*)malloc(zcap);
    if( !z )
    {
        free(raw);
        return 0;
    }
    size_t zlen = 0;
    z[zlen++] = 0x78;
    z[zlen++] = 0x01;
    for( size_t off = 0;; )
    {
        size_t n = rawlen - off;
        if( n > 65535u )
            n = 65535u;
        int final = (off + n >= rawlen);
        z[zlen++] = (unsigned char)final;
        z[zlen++] = (unsigned char)(n & 0xFFu);
        z[zlen++] = (unsigned char)((n >> 8) & 0xFFu);
        z[zlen++] = (unsigned char)(~n & 0xFFu);
        z[zlen++] = (unsigned char)((~n >> 8) & 0xFFu);
        if( n )
            memcpy(z + zlen, raw + off, n);
        zlen += n;
        off += n;
        if( final )
            break;
    }
    put_be32(z + zlen, adler32(raw, rawlen));
    zlen += 4;

    int ok = 0;
    FILE* f = fopen(path, "wb");
    if( f )
    {
        static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        unsigned char ihdr[13];
        put_be32(ihdr + 0, (unsigned int)w);
        put_be32(ihdr + 4, (unsigned int)h);
        ihdr[8] = 8;  /* bit depth */
        ihdr[9] = 6;  /* truecolour + alpha */
        ihdr[10] = 0;
        ihdr[11] = 0;
        ihdr[12] = 0;
        ok = fwrite(sig, 1, 8, f) == 8 && png_chunk(f, "IHDR", ihdr, 13) &&
             png_chunk(f, "IDAT", z, zlen) && png_chunk(f, "IEND", NULL, 0);
        fclose(f);
    }
    free(raw);
    free(z);
    return ok;
}

/* ------------------------------------------------------------ wire adoption */

static int
adopt_model(struct ToriDraw_Model* model)
{
    assert(model);
    struct EV_WireBuf buf = { 0 };
    int faces = 0;
    if( ev_wire_write_model(&buf, model) )
        faces = ev_set_model(buf.data, (int)buf.len);
    ev_wire_free(&buf);
    return faces;
}

static int
adopt_anim(struct ToriDraw_Animation* anim)
{
    assert(anim);
    struct EV_WireBuf buf = { 0 };
    int frames = 0;
    if( ev_wire_write_anim(&buf, anim) )
        frames = ev_set_anim(buf.data, (int)buf.len);
    ev_wire_free(&buf);
    return frames;
}

/* ------------------------------------------------------------- appearance */

/*
 * Fold a 12-slot appearance buffer into the player spec.
 *
 * The packing is the engine's canonical one from pkt_player_appearance.h --
 * APPEARANCE_PACK_KIT / APPEARANCE_PACK_OBJ -- and NOT the classic `0x100 +
 * kit` / `0x200 + obj` wire tags. That header explains why at length: the wire
 * ranges are only 256 wide, osrs239 ships 307 identity kits, and kit 300
 * packed the classic way reads back as a valid, wrong obj 44. The wire tags
 * are applied at the wire and nowhere else.
 *
 * A kit's slot does not say which body part it dresses -- the idk record does
 * -- so each one is loaded to read body_part_id. EV_PlayerSpec.kits is indexed
 * by that part, and worn objs simply accumulate in draw order.
 */
static int
apply_appearance(
    struct Tool_Dat2Cache* cache,
    struct EV_PlayerSpec* spec,
    const int* slots)
{
    assert(cache);
    assert(spec);
    assert(slots);

    spec->exact = 1;
    for( int s = 0; s < 12; s++ )
    {
        int packed = slots[s];
        enum AppearanceSlotKind kind = Appearance_SlotKind(packed);

        if( kind == APPEARANCE_SLOT_OBJ )
        {
            if( spec->worn_count >= EV_PLAYER_MAX_WORN )
            {
                fprintf(stderr, "slot %d: more than %d worn objs\n", s,
                        EV_PLAYER_MAX_WORN);
                return 0;
            }
            spec->worn[spec->worn_count++] = Appearance_SlotObj(packed);
        }
        else if( kind == APPEARANCE_SLOT_KIT )
        {
            int kit = Appearance_SlotKit(packed);
            struct RSCache_Dat2ConfigIdk* idk = ev_idk_load(cache, kit);
            if( !idk )
            {
                fprintf(stderr, "slot %d: identity kit %d does not decode\n", s, kit);
                return 0;
            }
            if( ev_player_kit_slot(idk->body_part_id) >= 0 )
                spec->kits[ev_player_kit_slot(idk->body_part_id)] = kit;
            else
                fprintf(stderr, "slot %d: kit %d dresses no body part (%d)\n", s,
                        kit, idk->body_part_id);
            RSCache_Dat2ConfigIdkFree(idk);
        }
        /* APPEARANCE_SLOT_EMPTY covers 0 and -1: the slot is simply unused. */
    }
    return 1;
}

/*
 * "a,b,c,..." -> up to 12 ints. Short lists leave the rest empty, which is how
 * a caller dresses only the slots it cares about.
 */
static int
parse_appearance(
    const char* text,
    int* out)
{
    assert(text);
    assert(out);

    int n = 0;
    const char* p = text;
    while( *p && n < 12 )
    {
        char* end = NULL;
        long v = strtol(p, &end, 0);
        if( end == p )
            return -1;
        out[n++] = (int)v;
        p = end;
        while( *p == ',' || *p == ' ' )
            p++;
    }
    return *p ? -1 : n;
}

/* --------------------------------------------------------- two-pass compose */

/*
 * Render one frame twice and resolve real coverage from the pair.
 *
 * `out` receives width*height*4 straight (un-premultiplied) RGBA. Returns the
 * number of pixels with any coverage.
 */
static int
compose_frame(
    unsigned char* out,
    int side,
    int yaw,
    int pitch,
    int zoom,
    int frame)
{
    assert(out);

    ev_set_bg(0xFF000000u);
    const uint8_t* black = ev_render(side, side, yaw, pitch, zoom, frame);
    if( !black )
        return 0;
    size_t n = (size_t)side * (size_t)side * 4u;
    unsigned char* keep = (unsigned char*)malloc(n);
    if( !keep )
        return 0;
    memcpy(keep, black, n); /* ev_render reuses its buffer */

    ev_set_bg(0xFFFFFFFFu);
    const uint8_t* white = ev_render(side, side, yaw, pitch, zoom, frame);
    if( !white )
    {
        free(keep);
        return 0;
    }

    int covered = 0;
    for( int i = 0; i < side * side; i++ )
    {
        int b0 = keep[i * 4 + 0], b1 = keep[i * 4 + 1], b2 = keep[i * 4 + 2];
        int w0 = white[i * 4 + 0], w1 = white[i * 4 + 1], w2 = white[i * 4 + 2];

        /* The lift is the same on every channel for a pure coverage blend;
         * averaging shrugs off a rounding step in the raster. */
        int lift = ((w0 - b0) + (w1 - b1) + (w2 - b2)) / 3;
        if( lift < 0 )
            lift = 0;
        if( lift > 255 )
            lift = 255;
        int alpha = 255 - lift;

        if( alpha <= 0 )
        {
            out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = out[i * 4 + 3] = 0;
            continue;
        }
        /* The black pass holds alpha*model, so dividing it back out gives the
         * model's own colour rather than one darkened by its own coverage. */
        int r = b0 * 255 / alpha, g = b1 * 255 / alpha, b = b2 * 255 / alpha;
        out[i * 4 + 0] = (unsigned char)(r > 255 ? 255 : r);
        out[i * 4 + 1] = (unsigned char)(g > 255 ? 255 : g);
        out[i * 4 + 2] = (unsigned char)(b > 255 ? 255 : b);
        out[i * 4 + 3] = (unsigned char)alpha;
        covered++;
    }
    free(keep);
    return covered;
}

struct EV_Box
{
    int x0, y0, x1, y1;
};

static void
box_reset(struct EV_Box* b)
{
    assert(b);
    b->x0 = b->y0 = 1 << 30;
    b->x1 = b->y1 = -1;
}

static void
box_add_coverage(
    struct EV_Box* b,
    const unsigned char* rgba,
    int side)
{
    assert(b);
    assert(rgba);
    for( int y = 0; y < side; y++ )
    {
        for( int x = 0; x < side; x++ )
        {
            if( !rgba[((size_t)y * (size_t)side + (size_t)x) * 4u + 3u] )
                continue;
            if( x < b->x0 )
                b->x0 = x;
            if( y < b->y0 )
                b->y0 = y;
            if( x > b->x1 )
                b->x1 = x;
            if( y > b->y1 )
                b->y1 = y;
        }
    }
}

static int
box_valid(const struct EV_Box* b)
{
    assert(b);
    return b->x1 >= b->x0 && b->y1 >= b->y0;
}

/* ------------------------------------------------------------------- usage */

static void
usage(const char* prog)
{
    printf(
        "Usage: %s --rev <name> <cache_dir> (--npc <id> | --player) --seq <id> [options]\n"
        "\n"
        "Renders an entity's animation to RGBA PNG frames with real alpha, plus\n"
        "frames.txt carrying each frame's duration. No server or browser.\n"
        "\n"
        "  --npc <id>       render an npc\n"
        "  --player         render a player instead\n"
        "  --loc <id>       render a loc (scenery) instead, in --shape\n"
        "  --shape <s>      the loc shape to build (10 centrepiece, 22 ground decor,\n"
        "                   ...); -1 = the record's first [-1]\n"
        "  --gender <0|1>   0 male, 1 female [0]\n"
        "  --appearance <list>\n"
        "                   up to 12 comma-separated appearance slots. Each is\n"
        "                   packed: 0x10000+kit for an identity kit, 0x20000+obj\n"
        "                   for worn equipment, 0 for empty. A kit's body part\n"
        "                   comes from its own record, not its slot. NOTE this is\n"
        "                   the engine's canonical packing, NOT the 256/512 wire\n"
        "                   tags -- see net/rev/packets/pkt_player_appearance.h.\n"
        "  --wear <obj>     equip one obj, on top of --appearance (repeatable)\n"
        "  --seq <id>       sequence to play; omit for the bind pose\n"
        "  --fit <px>       scale the subject so the animation's widest extent is\n"
        "                   about this many pixels [160]\n"
        "  --side <px>      render canvas; must exceed --fit [512]\n"
        "  --yaw <0-2047>   MODEL rotation. 0 south, 512 west, 1024 north,\n"
        "                   1536 east [0]\n"
        "  --model-pitch <0-2047>\n"
        "                   model pitch, tilting the model itself [0]\n"
        "  --roll <0-2047>  model roll [0]\n"
        "  --pitch <0-2047> CAMERA elevation, which moves the eye rather than\n"
        "                   the model [128]\n"
        "  --pad <px>       transparent margin around the crop [2]\n"
        "  --zoom <units>   fix the orbit distance instead of fitting to --fit.\n"
        "                   With --frame-height and --full, every run at the\n"
        "                   same values puts the model origin on the same pixel,\n"
        "                   so sequences and directions share one anchor\n"
        "  --frame-height <units>\n"
        "                   pin the framing lift instead of measuring each pose\n"
        "  --full           write the whole --side canvas, uncropped\n"
        "  --materials      also write iNNN.png, each face flat in a colour unique\n"
        "                   to it through the normal painter (which face won each\n"
        "                   pixel), zNNN.bin, each face's camera depth (float32),\n"
        "                   and faces.txt: id colour, base colour, head flag and\n"
        "                   vertices per face\n"
        "  --layer kit:<id>|obj:<id>\n"
        "                   with --player: draw ONE component of a player -- an\n"
        "                   identity kit or a worn obj -- posed and deformed\n"
        "                   inside the default body, so every layer of a body\n"
        "                   shares one pose, head line and anchor and the client\n"
        "                   can composite any appearance from them\n"
        "  --chibi <head%%>,<frac%%>,<body%%>[,<width%%>[,<inflate>]]\n"
        "                   scale the head (bind-pose top frac%% of the height)\n"
        "                   by head%% about the neck, the body height by body%%\n"
        "                   and its width by width%%; inflate pushes the body out\n"
        "                   along its normals by that many world units, for\n"
        "                   chunky limbs. e.g. 260,17,66,135,6\n"
        "  --out <dir>      output directory (required)\n",
        prog);
}

/* ------------------------------------------------------- texture flattening */

/*
 * A sprite render has no texture raster, so a textured face is drawn in its
 * texture's average colour -- the reference client's low-detail look. The
 * average is taken over the opaque texels, gamma undone (the HSL table bakes
 * brightness 0.8 in), and put back into HSL16 so the face is lit and grouped
 * like any other. Cached per texture id.
 */
static struct EV_TextureSet g_textures;
static int g_textures_loaded = 0;
static int g_flat_hsl[8192];

static int
rgb_to_hsl16(
    double r,
    double g,
    double b)
{
    r = pow(r, 1.0 / 0.8);
    g = pow(g, 1.0 / 0.8);
    b = pow(b, 1.0 / 0.8);
    double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
    double l = (mx + mn) / 2.0, h = 0.0, s = 0.0;
    if( mx > mn )
    {
        double d = mx - mn;
        s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
        if( mx == r )
            h = (g - b) / d + (g < b ? 6.0 : 0.0);
        else if( mx == g )
            h = (b - r) / d + 2.0;
        else
            h = (r - g) / d + 4.0;
        h /= 6.0;
    }
    int hh = (int)(h * 64.0) & 63;
    int ss = (int)(s * 8.0);
    int ll = (int)(l * 128.0);
    ss = ss > 7 ? 7 : ss;
    ll = ll > 127 ? 127 : ll;
    return (hh << 10) | (ss << 7) | ll;
}

static int
flatten_texture(
    int id,
    void* user)
{
    struct Tool_Dat2Cache* cache = (struct Tool_Dat2Cache*)user;
    assert(cache);
    if( id < 0 || id >= (int)(sizeof(g_flat_hsl) / sizeof(g_flat_hsl[0])) )
        return -1;
    if( !g_textures_loaded )
    {
        g_textures_loaded = ev_textures_load(cache, &g_textures) ? 1 : -1;
        for( int i = 0; i < (int)(sizeof(g_flat_hsl) / sizeof(g_flat_hsl[0])); i++ )
            g_flat_hsl[i] = -2;
    }
    if( g_textures_loaded < 0 )
        return -1;
    if( g_flat_hsl[id] != -2 )
        return g_flat_hsl[id];
    const struct EV_Texture* t = ev_textures_get(&g_textures, id);
    int result = -1;
    if( t && t->texels && t->size > 0 )
    {
        double sr = 0, sg = 0, sb = 0;
        long n = 0;
        for( int i = 0; i < t->size * t->size; i++ )
        {
            uint32_t p = (uint32_t)t->texels[i];
            if( !t->opaque && (p >> 24) < 128 )
                continue;
            sr += (p >> 16) & 0xFF;
            sg += (p >> 8) & 0xFF;
            sb += p & 0xFF;
            n++;
        }
        if( n > 0 )
            result = rgb_to_hsl16(sr / n / 255.0, sg / n / 255.0, sb / n / 255.0);
    }
    g_flat_hsl[id] = result;
    return result;
}

/* ------------------------------------------------------------ export passes */

/* Render one ev_set_material_pass mode and write its RGB over `crop`, whose
 * alpha (the lit frame's coverage) is kept. */
static int
write_pass(
    const char* out_dir,
    const char* prefix,
    int f,
    int mode,
    unsigned char* crop,
    const struct EV_Box* box,
    int side,
    int yaw,
    int pitch,
    int zoom,
    int seq_id)
{
    assert(crop);
    assert(box);
    int cw = box->x1 - box->x0 + 1;
    int ch = box->y1 - box->y0 + 1;
    ev_set_bg(0xFF000000u);
    ev_set_material_pass(mode);
    const uint8_t* mp = ev_render(side, side, yaw, pitch, zoom, seq_id >= 0 ? f : -1);
    ev_set_material_pass(0);
    assert(mp);
    for( int y = 0; y < ch; y++ )
        for( int x = 0; x < cw; x++ )
        {
            const uint8_t* src =
                mp + (((size_t)(box->y0 + y) * (size_t)side) + (size_t)(box->x0 + x)) * 4u;
            uint8_t* dst = crop + ((size_t)y * (size_t)cw + (size_t)x) * 4u;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s%03d.png", out_dir, prefix, f);
    if( !png_write_rgba(path, crop, cw, ch) )
    {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    return 1;
}

/* Per-face camera depth from the last face-ID pass, float32, face order. */
static int
write_face_depth(
    const char* out_dir,
    int f,
    int face_count)
{
    const float* depth = ev_face_depth();
    assert(depth);
    char path[1024];
    snprintf(path, sizeof(path), "%s/z%03d.bin", out_dir, f);
    FILE* zf = fopen(path, "wb");
    if( !zf || fwrite(depth, sizeof(float), (size_t)face_count, zf) != (size_t)face_count )
    {
        fprintf(stderr, "cannot write %s\n", path);
        if( zf )
            fclose(zf);
        return 0;
    }
    fclose(zf);
    return 1;
}

/* Per vertex: x, y relative to the written crop, and camera z, int32 each. */
static int
write_vertex_screen(
    const char* out_dir,
    int f,
    int vertex_count,
    const struct EV_Box* box)
{
    assert(box);
    const int* vs = ev_vertex_screen();
    assert(vs);
    int* rel = (int*)malloc(sizeof(int) * 3u * (size_t)vertex_count);
    assert(rel);
    for( int v = 0; v < vertex_count; v++ )
    {
        rel[v * 3 + 0] = vs[v * 3 + 0] - box->x0;
        rel[v * 3 + 1] = vs[v * 3 + 1] - box->y0;
        rel[v * 3 + 2] = vs[v * 3 + 2];
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/v%03d.bin", out_dir, f);
    FILE* vf = fopen(path, "wb");
    size_t n = (size_t)vertex_count * 3u;
    int ok = vf && fwrite(rel, sizeof(int), n, vf) == n;
    if( vf )
        fclose(vf);
    free(rel);
    if( !ok )
        fprintf(stderr, "cannot write %s\n", path);
    return ok;
}

/* The face table: what each face-ID colour means. Written after the first
 * pose, which is when the chibi deform has chosen its head. */
static int
write_faces(
    const char* out_dir,
    const struct ToriDraw_Model* model)
{
    assert(model);
    assert(model->face_colors);
    char path[1024];
    snprintf(path, sizeof(path), "%s/faces.txt", out_dir);
    FILE* ff = fopen(path, "wb");
    if( !ff )
    {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    fprintf(ff, "# face\tid_rgb\tbase_hsl16\tbase_rgb\thead\ta\tb\tc\n");
    for( int i = 0; i < model->face_count; i++ )
        fprintf(ff, "%d\t%06x\t%d\t%06x\t%d\t%d\t%d\t%d\n", i, ev_face_code_rgb(i),
                model->face_colors[i], ToriDraw_Hsl16ToRgb(model->face_colors[i]) & 0xFFFFFF,
                ev_face_is_head(model, i), model->face_indices_a[i], model->face_indices_b[i],
                model->face_indices_c[i]);
    fclose(ff);
    return 1;
}

/* -------------------------------------------------------------------- main */

int
main(
    int argc,
    char** argv)
{
    const char* rev_name = NULL;
    const char* cache_dir = NULL;
    const char* out_dir = NULL;
    int npc_id = -1;
    int as_player = 0;
    int loc_id = -1;
    int loc_shape = -1;
    int seq_id = -1;
    int fit = 160;
    int side = 512;
    int yaw = 0;
    int pitch = 128;
    int model_pitch = 0;
    int roll = 0;
    int pad = 2;
    int fixed_zoom = 0;
    int frame_height = 0;
    int full = 0;
    int chibi[5] = { 0, 0, 0, 0, 0 };
    int materials = 0;
    int layer_obj = -1;
    int layer_id = -1;
    int gender = 0;
    int worn[EV_SHEET_MAX_WORN];
    int worn_count = 0;
    int appearance[12];
    int appearance_count = 0;

    for( int i = 0; i < 12; i++ )
        appearance[i] = 0;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            rev_name = argv[++i];
        else if( strcmp(argv[i], "--npc") == 0 && i + 1 < argc )
            npc_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--loc") == 0 && i + 1 < argc )
            loc_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--shape") == 0 && i + 1 < argc )
            loc_shape = atoi(argv[++i]);
        else if( strcmp(argv[i], "--player") == 0 )
            as_player = 1;
        else if( strcmp(argv[i], "--wear") == 0 && i + 1 < argc )
        {
            if( worn_count >= EV_SHEET_MAX_WORN )
            {
                fprintf(stderr, "too many --wear objs\n");
                return 2;
            }
            worn[worn_count++] = atoi(argv[++i]);
        }
        else if( strcmp(argv[i], "--seq") == 0 && i + 1 < argc )
            seq_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--fit") == 0 && i + 1 < argc )
            fit = atoi(argv[++i]);
        else if( strcmp(argv[i], "--side") == 0 && i + 1 < argc )
            side = atoi(argv[++i]);
        else if( strcmp(argv[i], "--yaw") == 0 && i + 1 < argc )
            yaw = atoi(argv[++i]);
        else if( strcmp(argv[i], "--pitch") == 0 && i + 1 < argc )
            pitch = atoi(argv[++i]);
        else if( strcmp(argv[i], "--model-pitch") == 0 && i + 1 < argc )
            model_pitch = atoi(argv[++i]);
        else if( strcmp(argv[i], "--roll") == 0 && i + 1 < argc )
            roll = atoi(argv[++i]);
        else if( strcmp(argv[i], "--gender") == 0 && i + 1 < argc )
            gender = atoi(argv[++i]);
        else if( strcmp(argv[i], "--appearance") == 0 && i + 1 < argc )
        {
            appearance_count = parse_appearance(argv[++i], appearance);
            if( appearance_count < 0 )
            {
                fprintf(stderr, "--appearance wants up to 12 comma-separated ints\n");
                return 2;
            }
        }
        else if( strcmp(argv[i], "--pad") == 0 && i + 1 < argc )
            pad = atoi(argv[++i]);
        else if( strcmp(argv[i], "--zoom") == 0 && i + 1 < argc )
            fixed_zoom = atoi(argv[++i]);
        else if( strcmp(argv[i], "--frame-height") == 0 && i + 1 < argc )
            frame_height = atoi(argv[++i]);
        else if( strcmp(argv[i], "--full") == 0 )
            full = 1;
        else if( strcmp(argv[i], "--materials") == 0 )
            materials = 1;
        else if( strcmp(argv[i], "--layer") == 0 && i + 1 < argc )
        {
            const char* v = argv[++i];
            if( strncmp(v, "kit:", 4) == 0 )
                layer_obj = 0;
            else if( strncmp(v, "obj:", 4) == 0 )
                layer_obj = 1;
            else
            {
                fprintf(stderr, "--layer wants kit:<id> or obj:<id>\n");
                return 2;
            }
            layer_id = atoi(v + 4);
        }
        else if( strcmp(argv[i], "--chibi") == 0 && i + 1 < argc )
        {
            if( sscanf(argv[++i], "%d,%d,%d,%d,%d", &chibi[0], &chibi[1], &chibi[2], &chibi[3],
                       &chibi[4]) < 3 )
            {
                fprintf(stderr, "--chibi wants head%%,frac%%,body%%[,width%%[,inflate]]\n");
                return 2;
            }
        }
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_dir = argv[++i];
        else if( strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0 )
        {
            usage(argv[0]);
            return 0;
        }
        else if( argv[i][0] != '-' && !cache_dir )
            cache_dir = argv[i];
        else
        {
            fprintf(stderr, "unexpected argument '%s'\n", argv[i]);
            return 2;
        }
    }

    if( !rev_name || !cache_dir || !out_dir || (npc_id < 0 && !as_player && loc_id < 0) )
    {
        usage(argv[0]);
        return 2;
    }
    if( side <= fit )
    {
        fprintf(stderr, "--side (%d) must exceed --fit (%d)\n", side, fit);
        return 2;
    }
    if( side > EV_SHEET_MAX_SIDE )
    {
        fprintf(stderr, "--side %d is too large\n", side);
        return 2;
    }

    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    if( !tool_resolve_profile(rev_name, NULL, NULL, NULL, NULL, &profile) )
    {
        fprintf(stderr, "unknown revision '%s'\n", rev_name);
        return 1;
    }
    if( !tool_dat2_open(cache_dir, &profile, &cache) )
    {
        fprintf(stderr, "cannot open cache at %s\n", cache_dir);
        return 1;
    }
    ToriDraw_Init();
    ev_init();
    ev_build_set_texture_flatten(flatten_texture, &cache);
    /* --yaw is already the model's own rotation (ev_render sets
     * ToriDraw_Position.yaw with it); these are the other two axes of the same
     * orientation. --pitch, separately, elevates the camera. */
    ev_set_orientation(model_pitch, roll);
    if( frame_height > 0 )
        ev_set_frame_height(frame_height);
    ev_set_chibi(chibi[0], chibi[1], chibi[2], chibi[3]);
    ev_set_chibi_inflate(chibi[4]);

    struct ToriDraw_Model* model = NULL;
    if( as_player )
    {
        struct EV_PlayerSpec spec;
        struct EV_PlayerPartMap map;
        ev_player_spec_init(&spec);
        spec.gender = gender;
        /* The appearance buffer goes on first so an explicit --wear can still
         * add to what it dressed. */
        if( appearance_count > 0 && !apply_appearance(&cache, &spec, appearance) )
            return 1;
        for( int i = 0; i < worn_count; i++ )
        {
            if( spec.worn_count >= EV_PLAYER_MAX_WORN )
            {
                fprintf(stderr, "more than %d worn objs\n", EV_PLAYER_MAX_WORN);
                return 1;
            }
            spec.worn[spec.worn_count++] = worn[i];
        }
        if( layer_id >= 0 )
        {
            /* The reference body is the full default body, drawn exactly (no
             * hiding): the layer is posed among the parts it will be
             * composited with, and nothing it covers moves the pose. */
            if( layer_obj )
                spec.worn[spec.worn_count++] = layer_id;
            else
            {
                struct RSCache_Dat2ConfigIdk* idk = ev_idk_load(&cache, layer_id);
                if( !idk || ev_player_kit_slot(idk->body_part_id) < 0 )
                {
                    fprintf(stderr, "kit %d does not decode to a body part\n", layer_id);
                    return 1;
                }
                spec.kits[ev_player_kit_slot(idk->body_part_id)] = layer_id;
                RSCache_Dat2ConfigIdkFree(idk);
            }
            ev_resolve_default_kits(&cache, spec.kits, spec.gender);
            spec.exact = 1;
        }
        model = ev_build_player_model(&cache, &spec, &map);
        if( model && layer_id >= 0 )
        {
            int first = -1, count = 0, kit_vertices = 0;
            for( int p = 0; p < map.count; p++ )
            {
                const struct EV_PlayerPart* pt = &map.parts[p];
                if( !pt->is_obj )
                    kit_vertices = pt->vertex_first + pt->vertex_count;
                if( pt->source_id == layer_id && pt->is_obj == layer_obj )
                {
                    if( first < 0 )
                        first = pt->face_first;
                    count = pt->face_first + pt->face_count - first;
                }
            }
            if( first < 0 )
            {
                fprintf(stderr, "layer %s:%d contributed no faces\n", layer_obj ? "obj" : "kit",
                        layer_id);
                return 1;
            }
            ev_set_face_range(first, count);
            ev_set_chibi_reference(kit_vertices);
            /* A weapon or a shield keeps its silhouette under inflation. */
            if( layer_obj )
            {
                struct RSCache_Dat2ConfigObj* obj = ev_obj_load(&cache, layer_id);
                assert(obj);
                if( obj->wearpos_1 == 3 || obj->wearpos_1 == 5 )
                {
                    int vfirst = -1, vcount = 0;
                    for( int p = 0; p < map.count; p++ )
                    {
                        const struct EV_PlayerPart* pt = &map.parts[p];
                        if( pt->source_id == layer_id && pt->is_obj )
                        {
                            if( vfirst < 0 )
                                vfirst = pt->vertex_first;
                            vcount = pt->vertex_first + pt->vertex_count - vfirst;
                        }
                    }
                    ev_set_chibi_inflate_skip(vfirst, vcount);
                }
                RSCache_Dat2ConfigObjFree(obj);
            }
        }
    }
    else if( loc_id >= 0 )
    {
        model = ev_build_loc_model(&cache, loc_id, loc_shape);
    }
    else
    {
        model = ev_build_npc_model(&cache, npc_id);
    }
    if( !model )
    {
        fprintf(stderr, "could not build the %s model\n",
                as_player ? "player" : (loc_id >= 0 ? "loc" : "npc"));
        return 1;
    }
    if( !adopt_model(model) )
    {
        fprintf(stderr, "the model has no drawable faces\n");
        return 1;
    }

    int frame_count = 1;
    if( seq_id >= 0 )
    {
        int framemap = -1;
        struct ToriDraw_Animation* anim = ev_build_seq_anim(&cache, seq_id, &framemap);
        if( !anim )
        {
            fprintf(stderr, "sequence %d did not build\n", seq_id);
            return 1;
        }
        if( !adopt_anim(anim) )
        {
            fprintf(stderr, "sequence %d has no frames\n", seq_id);
            return 1;
        }
        frame_count = ev_frame_count();
        if( frame_count > EV_SHEET_MAX_FRAMES )
            frame_count = EV_SHEET_MAX_FRAMES;
    }

    size_t canvas = (size_t)side * (size_t)side * 4u;
    unsigned char* buf = (unsigned char*)malloc(canvas);
    if( !buf )
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    /*
     * Fit the camera to the union of every frame, not to one of them. A
     * sequence that raises an arm is wider on some frames than others, and
     * fitting to a single frame clips the rest.
     *
     * Coverage scales roughly as 1/zoom, so one corrective step lands close;
     * a couple more converge without costing anything noticeable.
     */
    int zoom = fixed_zoom > 0 ? fixed_zoom : 512;
    for( int pass = 0; fixed_zoom <= 0 && pass < 3; pass++ )
    {
        struct EV_Box b;
        box_reset(&b);
        for( int f = 0; f < frame_count; f++ )
        {
            if( compose_frame(buf, side, yaw, pitch, zoom, seq_id >= 0 ? f : -1) )
                box_add_coverage(&b, buf, side);
        }
        if( !box_valid(&b) )
        {
            fprintf(stderr, "nothing was drawn at zoom %d\n", zoom);
            return 1;
        }
        int extent = (b.x1 - b.x0) > (b.y1 - b.y0) ? (b.x1 - b.x0) : (b.y1 - b.y0);
        if( extent < 1 )
            extent = 1;
        long next = (long)zoom * extent / (fit > 0 ? fit : 1);
        if( next < 1 )
            next = 1;
        if( next == zoom )
            break;
        zoom = (int)next;
    }

    /* Final geometry, at the zoom just settled on. */
    struct EV_Box box;
    box_reset(&box);
    for( int f = 0; f < frame_count; f++ )
    {
        if( compose_frame(buf, side, yaw, pitch, zoom, seq_id >= 0 ? f : -1) )
            box_add_coverage(&box, buf, side);
    }
    /* Under --full an empty frame is an answer, not a failure: a layer can be
     * wholly hidden from one side (the jaw from behind) and the frame still
     * has to exist so every layer of a body has the same frames. */
    if( !box_valid(&box) && !full )
    {
        fprintf(stderr, "nothing was drawn\n");
        return 1;
    }
    if( !box_valid(&box) )
    {
        box.x0 = 0;
        box.y0 = 0;
        box.x1 = side - 1;
        box.y1 = side - 1;
    }
    box.x0 -= pad;
    box.y0 -= pad;
    box.x1 += pad;
    box.y1 += pad;
    if( box.x0 < 0 )
        box.x0 = 0;
    if( box.y0 < 0 )
        box.y0 = 0;
    if( box.x1 > side - 1 )
        box.x1 = side - 1;
    if( box.y1 > side - 1 )
        box.y1 = side - 1;

    /*
     * A subject touching the canvas edge was clipped by the canvas, and the
     * crop cannot tell that from a subject that merely reaches the edge. Say
     * so rather than writing a quietly truncated sheet -- a cut-off limb looks
     * exactly like a downstream bug, and has cost real time.
     */
    if( box.x0 == 0 || box.y0 == 0 || box.x1 == side - 1 || box.y1 == side - 1 )
        fprintf(stderr,
                "warning: the subject reaches the canvas edge; raise --side "
                "or lower --fit or it is being clipped\n");

    /* A shared anchor needs a shared frame: cropping to this run's own
     * coverage would put the origin somewhere different in every run. */
    if( full )
    {
        box.x0 = 0;
        box.y0 = 0;
        box.x1 = side - 1;
        box.y1 = side - 1;
    }
    int cw = box.x1 - box.x0 + 1;
    int ch = box.y1 - box.y0 + 1;

    unsigned char* crop = (unsigned char*)malloc((size_t)cw * (size_t)ch * 4u);
    if( !crop )
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/frames.txt", out_dir);
    FILE* tf = fopen(path, "wb");
    if( !tf )
    {
        fprintf(stderr, "cannot write into '%s' -- does it exist?\n", out_dir);
        return 1;
    }
    fprintf(tf, "# frame\tticks\tms\n");


    int total_ticks = 0;
    for( int f = 0; f < frame_count; f++ )
    {
        if( !compose_frame(buf, side, yaw, pitch, zoom, seq_id >= 0 ? f : -1) && !full )
            continue;
        for( int y = 0; y < ch; y++ )
        {
            memcpy(crop + (size_t)y * (size_t)cw * 4u,
                   buf + (((size_t)(box.y0 + y) * (size_t)side) + (size_t)box.x0) * 4u,
                   (size_t)cw * 4u);
        }
        snprintf(path, sizeof(path), "%s/f%03d.png", out_dir, f);
        if( !png_write_rgba(path, crop, cw, ch) )
        {
            fprintf(stderr, "cannot write %s\n", path);
            fclose(tf);
            return 1;
        }
        if( materials )
        {
            /* The lit frame's alpha is the coverage; the face-ID pass only says
             * which face won each covered pixel. */
            if( !write_pass(out_dir, "i", f, 1, crop, &box, side, yaw, pitch, zoom, seq_id) ||
                !write_face_depth(out_dir, f, model->face_count) ||
                !write_vertex_screen(out_dir, f, model->vertex_count, &box) ||
                (f == 0 && !write_faces(out_dir, model)) )
            {
                fclose(tf);
                return 1;
            }
        }
        /* A tick is 20ms. Zero means the sequence did not say, and one tick is
         * the shortest thing it could have meant. */
        int ticks = (seq_id >= 0) ? ev_frame_delay(f) : 0;
        if( ticks <= 0 )
            ticks = 1;
        total_ticks += ticks;
        fprintf(tf, "%d\t%d\t%d\n", f, ticks, ticks * 20);
    }
    fclose(tf);

    printf("%s %d, seq %d: %d frames, %dx%d, zoom %d, %d ticks (%dms) -> %s\n",
           as_player ? "player" : "npc", as_player ? 0 : npc_id, seq_id,
           frame_count, cw, ch, zoom, total_ticks, total_ticks * 20, out_dir);

    free(buf);
    free(crop);
    return 0;
}
