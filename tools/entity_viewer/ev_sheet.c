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
#include "ev_wire.h"

#include "asset_access.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "rscache.h"
#include "tool_profile.h"
#include "toridraw.h"

#include <assert.h>
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
            if( idk->body_part_id >= 0 && idk->body_part_id < EV_PLAYER_PARTS )
                spec->kits[idk->body_part_id] = kit;
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
        "  --out <dir>      output directory (required)\n",
        prog);
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
    int seq_id = -1;
    int fit = 160;
    int side = 512;
    int yaw = 0;
    int pitch = 128;
    int model_pitch = 0;
    int roll = 0;
    int pad = 2;
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

    if( !rev_name || !cache_dir || !out_dir || (npc_id < 0 && !as_player) )
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
    /* --yaw is already the model's own rotation (ev_render sets
     * ToriDraw_Position.yaw with it); these are the other two axes of the same
     * orientation. --pitch, separately, elevates the camera. */
    ev_set_orientation(model_pitch, roll);

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
        model = ev_build_player_model(&cache, &spec, &map);
    }
    else
    {
        model = ev_build_npc_model(&cache, npc_id);
    }
    if( !model )
    {
        fprintf(stderr, "could not build the %s model\n", as_player ? "player" : "npc");
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
    int zoom = 512;
    for( int pass = 0; pass < 3; pass++ )
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
    if( !box_valid(&box) )
    {
        fprintf(stderr, "nothing was drawn\n");
        return 1;
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
    int cw = box.x1 - box.x0 + 1;
    int ch = box.y1 - box.y0 + 1;

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
        if( !compose_frame(buf, side, yaw, pitch, zoom, seq_id >= 0 ? f : -1) )
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
