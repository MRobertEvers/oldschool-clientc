/*
 * Does a loaded texture actually reach the framebuffer?
 *
 * The failure this exists for is "everything renders white": a model whose
 * faces name textures, drawn with no texels to sample, comes out as flat
 * untextured colour and nothing in any count says so. So this renders the SAME
 * model twice — once with the texture set installed and once without — and
 * compares the pixels. Identical output means the textures did nothing,
 * regardless of how many loaded.
 */
#include "ev_build.h"
#include "ev_render.h"
#include "ev_textures.h"
#include "ev_wire.h"
#include "asset_access.h"
#include "tool_profile.h"
#include "toridraw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
dump_stats(const char* when)
{
    static const char* names[] = { "faces","untextured","plane","cyl","cube","sphere",
        "no_texels","no_mapping","hidden","alpha","g_opaque","g_trans","g_alpha",
        "facealpha","modulate" };
    if( !ev_model_hd_active() ) return;
    const int* st = ev_hd_stats();
    printf("  routing %-7s:", when);
    for( int i = 0; i < ev_hd_stats_count() && i < 15; i++ )
        if( st[i] ) printf(" %s=%d", names[i], st[i]);
    printf("\n");
}

static struct EV_TextureSet g_set;

static int
have_texture(int id, void* user)
{
    (void)user;
    return ev_textures_get(&g_set, id) != NULL;
}

/* An EVT1 blob for every id the model names, built the way the server does. */
static uint8_t*
build_blob(const struct ToriDraw_Model* m, size_t* out_len)
{
    int ids[256], n = 0;
    for( int f = 0; f < m->face_count; f++ )
    {
        int id = m->face_textures ? m->face_textures[f] : -1;
        int dup = 0;
        if( id < 0 ) continue;
        for( int i = 0; i < n; i++ ) if( ids[i] == id ) dup = 1;
        if( !dup && n < 256 ) ids[n++] = id;
    }

    size_t cap = 8;
    for( int i = 0; i < n; i++ )
    {
        const struct EV_Texture* t = ev_textures_get(&g_set, ids[i]);
        if( t ) cap += 8 + (size_t)t->size * t->size * 4;
    }
    uint8_t* b = malloc(cap);
    size_t at = 0;
    uint32_t magic = 0x31545645u;
    memcpy(b + at, &magic, 4); at += 4;
    uint32_t count = 0;
    size_t count_at = at; at += 4;
    for( int i = 0; i < n; i++ )
    {
        const struct EV_Texture* t = ev_textures_get(&g_set, ids[i]);
        if( !t ) continue;
        uint32_t id32 = (uint32_t)ids[i];
        memcpy(b + at, &id32, 4); at += 4;
        b[at++] = (uint8_t)(t->size & 0xFF);
        b[at++] = (uint8_t)((t->size >> 8) & 0xFF);
        b[at++] = (uint8_t)(t->opaque ? 1 : 0);
        b[at++] = 0;
        for( int p = 0; p < t->size * t->size; p++ )
        {
            uint32_t v = (uint32_t)t->texels[p];
            memcpy(b + at, &v, 4); at += 4;
        }
        count++;
    }
    memcpy(b + count_at, &count, 4);
    *out_len = at;
    printf("  blob: %u textures, %zu bytes\n", count, at);
    return b;
}

/* Distinct colours and the share of the frame that is one flat colour: a
 * textured surface has many colours, an untextured one has a handful. */
static void
describe(const unsigned char* rgba, int w, int h, const char* label)
{
    /* Coarse histogram: exact distinct counts are dominated by the shading
     * ramp, which both cases have. */
    static int bucket[32 * 32 * 32];
    memset(bucket, 0, sizeof(bucket));
    int drawn = 0;
    for( int i = 0; i < w * h; i++ )
    {
        int r = rgba[i*4+0], g = rgba[i*4+1], b = rgba[i*4+2];
        if( r == 0x14 && g == 0x18 && b == 0x21 ) continue; /* background */
        drawn++;
        bucket[(r >> 3) * 1024 + (g >> 3) * 32 + (b >> 3)]++;
    }
    int used = 0, top = 0;
    for( int i = 0; i < 32*32*32; i++ ) { if( bucket[i] ) used++; if( bucket[i] > top ) top = bucket[i]; }
    printf("  %-18s drawn=%-6d colour cells=%-5d largest cell=%.1f%%  centre=%02X%02X%02X corner=%02X%02X%02X\n",
           label, drawn, used, drawn ? 100.0 * top / drawn : 0.0,
           rgba[(h/2*w + w/2)*4], rgba[(h/2*w + w/2)*4+1], rgba[(h/2*w + w/2)*4+2],
           rgba[0], rgba[1], rgba[2]);
}

int
main(int argc, char** argv)
{
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    int npc_id = argc > 3 ? atoi(argv[3]) : 15220;

    /*
     * Before anything builds a model, not just before rendering: the build does
     * lighting and measures the bounds cylinder, and both read the trig tables.
     * Built first, the model gets a garbage cylinder and is culled at every
     * camera — which looks like "nothing renders" rather than "init is missing".
     */
    ToriDraw_Init();

    if( !tool_resolve_profile(argv[2], NULL, NULL, NULL, NULL, &profile) ) return 1;
    if( !tool_dat2_open(argv[1], &profile, &cache) ) return 1;
    if( !ev_textures_load(&cache, &g_set) ) return 1;

    ev_build_set_texture_available(have_texture, NULL);

    /* The same choice the server makes: HD when the npc has mapped faces. */
    struct ToriDraw_ModelHD* hd = ev_build_npc_model_hd(&cache, npc_id);
    struct ToriDraw_Model* model =
        hd ? &hd->base : ev_build_npc_model(&cache, npc_id);
    printf("  build: %s\n", hd ? "HD (mapped textures)" : "plain");
    if( !model ) { printf("no model for npc %d\n", npc_id); return 1; }

    int textured = 0;
    for( int f = 0; f < model->face_count; f++ )
        if( model->face_textures && model->face_textures[f] >= 0 ) textured++;
    printf("npc %d: %d faces, %d textured\n", npc_id, model->face_count, textured);

    struct EV_WireBuf wb = { 0 };
    if( hd )
        ev_wire_write_model_hd(&wb, hd);
    else
        ev_wire_write_model(&wb, model);
    size_t blob_len = 0;
    uint8_t* blob = build_blob(model, &blob_len);
    if( hd )
        ToriDraw_ModelHDFree(hd);
    else
        ToriDraw_ModelFree(model);

    const int W = 256, H = 256;

    ev_init();
    int loaded_faces = ev_wire_is_model_hd(wb.data, wb.len)
                           ? ev_set_model_hd(wb.data, (int)wb.len)
                           : ev_set_model(wb.data, (int)wb.len);
    printf("  ev_set_model -> %d faces\n", loaded_faces);

    /* Frame it the way the page does: the zoom is derived from the model's own
     * height, and a fixed guess leaves the subject off-screen — which looks
     * exactly like "the textures did nothing". */
    int mh = ev_model_height();
    int zoom = mh * 16 / 10;
    if( zoom < 260 ) zoom = 260;
    if( zoom > 6000 ) zoom = 6000;
    printf("  model height=%d, zoom=%d\n", mh, zoom);

    /* Sweep before measuring: a subject that never appears at the chosen
     * framing makes every later comparison vacuous. */
    for( int z = 100; z <= 4000; z = z * 2 )
        for( int pi = 64; pi <= 512; pi += 192 )
        {
            unsigned char* t = ev_render(W, H, 300, pi, z, 0);
            int nonbg = 0;
            for( int i = 0; i < W * H; i++ )
                if( t[i*4] != 0x14 || t[i*4+1] != 0x18 || t[i*4+2] != 0x21 ) nonbg++;
            printf("    sweep zoom=%-5d pitch=%-4d non-background px=%d\n", z, pi, nonbg);
        }

    unsigned char* a = ev_render(W, H, 300, 128, zoom, 0);
    unsigned char* copy = malloc((size_t)W * H * 4);
    memcpy(copy, a, (size_t)W * H * 4);
    printf("  cull=%d (0=visible)\n", ev_last_cull());
    dump_stats("before");
    describe(copy, W, H, "no textures");

    int n = ev_set_textures(blob, (int)blob_len);
    printf("  installed %d textures\n", n);
    unsigned char* b = ev_render(W, H, 300, 128, zoom, 0);
    dump_stats("after");
    describe(b, W, H, "with textures");

    int diff = 0;
    for( int i = 0; i < W * H * 4; i++ ) if( copy[i] != b[i] ) diff++;
    printf("  bytes differing: %d of %d (%.1f%%)\n", diff, W*H*4, 100.0*diff/(W*H*4));
    printf("  VERDICT: %s\n", diff > 0 ? "textures reach the framebuffer"
                                       : "TEXTURES CHANGED NOTHING");
    return diff > 0 ? 0 : 2;
}
