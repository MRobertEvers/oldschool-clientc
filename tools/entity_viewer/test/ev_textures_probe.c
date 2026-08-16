#include "ev_textures.h"
#include "asset_access.h"
#include "tool_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include "ev_build.h"
#include "toridraw.h"
#include "toridraw_hsl16.h"

static int keep_all(int id, void* user) { (void)id; (void)user; return 1; }

int main(int argc, char** argv)
{
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    struct EV_TextureSet set;

    ToriDraw_Init(); /* the hsl16->rgb table is runtime-built */
    if( !tool_resolve_profile(argv[2], NULL, NULL, NULL, NULL, &profile) ) return 1;
    if( !tool_dat2_open(argv[1], &profile, &cache) ) { printf("open failed\n"); return 1; }
    if( !ev_textures_load(&cache, &set) ) printf("load returned false\n");

    printf("  set: count=%d loaded=%d failed=%d procedural=%d\n",
           set.count, set.loaded, set.failed, set.procedural_system);

    /* Non-black, non-uniform: a texture that is all one colour is the failure
     * the user is looking at (everything white). */
    int nonuniform = 0, alltransparent = 0, sampled = 0;
    for( int i = 0; i < set.count && sampled < 100000; i++ )
    {
        const int32_t* t = set.items[i].texels;
        if( !t ) continue;
        sampled++;
        int32_t first = t[0]; int differs = 0, opaque_px = 0;
        for( int p = 0; p < set.items[i].size * set.items[i].size; p++ ) {
            if( t[p] != first ) differs = 1;
            if( (t[p] >> 24) & 0xFF ) opaque_px++;
        }
        if( differs ) nonuniform++;
        if( !opaque_px ) alltransparent++;
    }
    int opaque_n = 0, partial = 0;
    for( int i = 0; i < set.count; i++ )
    {
        if( !set.items[i].texels ) continue;
        if( set.items[i].opaque ) opaque_n++;
        else {
            int tp = 0;
            for( int p = 0; p < set.items[i].size * set.items[i].size; p++ )
                if( ((set.items[i].texels[p] >> 24) & 0xFF) != 0xFF ) tp++;
            if( tp ) partial++;
        }
    }
    printf("  of %d baked: %d non-uniform, %d fully transparent\n", sampled, nonuniform, alltransparent);
    printf("  opaque=%d  non-opaque=%d (of which %d really have non-255 alpha)\n",
           opaque_n, set.count - opaque_n, partial);

    /* How transparent are the non-opaque ones, really? A texture that is 99%%
     * alpha-0 draws as nothing through the alpha gate, which is the difference
     * between "has some cutouts" and "is invisible". */
    {
        long long total = 0, zero = 0, full = 0, mid = 0;
        int mostly_clear = 0;
        for( int i = 0; i < set.count; i++ )
        {
            if( !set.items[i].texels ) continue;
            int n = set.items[i].size * set.items[i].size, z = 0;
            for( int p = 0; p < n; p++ )
            {
                int a = (set.items[i].texels[p] >> 24) & 0xFF;
                total++;
                if( a == 0 ) { zero++; z++; }
                else if( a == 255 ) full++;
                else mid++;
            }
            if( z * 2 > n ) mostly_clear++;
        }
        printf("  texels: alpha0=%.1f%%  alpha255=%.1f%%  partial=%.1f%%   "
               "textures >50%% clear: %d\n",
               100.0 * zero / total, 100.0 * full / total, 100.0 * mid / total, mostly_clear);
    }

    for( int i = 0; i < set.count && i < 3; i++ ) {
        const struct EV_Texture* t = &set.items[i];
        printf("  id=%-5d %dx%d opaque=%d proc=%d  px[0]=%08X px[mid]=%08X px[end]=%08X\n",
            t->id, t->size, t->size, t->opaque, t->procedural,
            t->texels[0], t->texels[t->size*t->size/2], t->texels[t->size*t->size-1]);
    }
    /* Do the model side too: an npc whose faces name no texture id would make
     * the loader look broken when it is not. */
    if( argc > 3 )
    {
        /* Keep every id: the question here is what the MODEL names, and a
         * predicate that says "none" strips them all and answers itself. */
        ev_build_set_texture_available(keep_all, NULL);
        for( int a = 3; a < argc; a++ )
        {
            int npc_id = atoi(argv[a]);
            struct ToriDraw_Model* m = ev_build_npc_model(&cache, npc_id);
            if( !m ) { printf("  npc %d: no model\n", npc_id); continue; }
            int textured = 0, distinct[64], dn = 0, present = 0;
            for( int f = 0; f < m->face_count; f++ ) {
                int id = m->face_textures ? m->face_textures[f] : -1;
                if( id < 0 ) continue;
                textured++;
                int dup = 0;
                for( int i = 0; i < dn; i++ ) if( distinct[i] == id ) dup = 1;
                if( !dup && dn < 64 ) { distinct[dn++] = id; if (ev_textures_get(&set,id)) present++; }
            }
            /* The render-type split decides which path can draw the face at
             * all: the classic raster only does plane (type 0) mapping. */
            int rt[4] = { 0, 0, 0, 0 }, coords_ok = 0;
            for( int t = 0; t < m->textured_face_count; t++ )
            {
                int k = m->texture_render_types ? m->texture_render_types[t] : 0;
                if( k >= 0 && k < 4 ) rt[k]++;
            }
            for( int f = 0; f < m->face_count; f++ )
                if( m->face_texture_coords && m->face_texture_coords[f] >= 0 ) coords_ok++;
            /* Shade histogram for TEXTURED faces. These are 0..127 lightness,
             * not HSL16 — if they cluster at the top the model renders flat and
             * washed out no matter what the texels contain. */
            int hist[8] = { 0 }, tex_n = 0, flatn = 0;
            for( int f = 0; f < m->face_count; f++ )
            {
                if( !m->face_textures || m->face_textures[f] < 0 ) continue;
                int sa = m->face_colors_a ? m->face_colors_a[f] : 0;
                if( m->face_colors_c && m->face_colors_c[f] == -1 ) flatn++;
                if( sa < 0 ) sa = 0; if( sa > 127 ) sa = 127;
                hist[sa >> 4]++; tex_n++;
            }
            /* The authored per-face colour, which lighting does NOT overwrite:
             * face_colors is the flat HSL16 the model shipped with. If the
             * texture is a greyscale mask, this is the only place a hue can
             * come from. */
            printf("    authored hue for faces using each texture:\n");
            for( int t = 0; t < 8; t++ )
            {
                int want = -1, seen = 0;
                for( int f = 0; f < m->face_count && seen < 1; f++ )
                {
                    if( !m->face_textures || m->face_textures[f] < 0 ) continue;
                    int dup = 0;
                    for( int u = 0; u < t; u++ ) { (void)u; }
                    want = m->face_textures[f];
                    (void)dup; seen = 1;
                }
                break;
            }
            {
                int ids2[16], n2 = 0;
                for( int f = 0; f < m->face_count; f++ )
                {
                    int tid = m->face_textures ? m->face_textures[f] : -1;
                    if( tid < 0 ) continue;
                    int dup = 0;
                    for( int u = 0; u < n2; u++ ) if( ids2[u] == tid ) dup = 1;
                    if( dup || n2 >= 16 ) continue;
                    ids2[n2++] = tid;
                    int hsl = m->face_colors ? m->face_colors[f] : -1;
                    int rgb = hsl >= 0 ? ToriDraw_Hsl16ToRgb(hsl) : -1;
                    printf("      texture %-5d -> face_colors hsl16=%-6d rgb=%06X\n",
                           tid, hsl, rgb < 0 ? 0 : rgb);
                }
            }
            printf("    shade histogram (0..127, textured faces, n=%d, flat=%d):", tex_n, flatn);
            for( int i = 0; i < 8; i++ ) printf(" [%d-%d]=%d", i*16, i*16+15, hist[i]);
            printf("\n");
            printf("  npc %-6d faces=%-5d textured=%-5d texfaces=%-4d rt[plane=%d cyl=%d cube=%d sph=%d] coords=%-5d distinct ids=%-3d in cache=%d ids:",
                   npc_id, m->face_count, textured, m->textured_face_count,
                   rt[0], rt[1], rt[2], rt[3], coords_ok, dn, present);
            for( int i = 0; i < dn && i < 10; i++ ) printf(" %d", distinct[i]);
            printf("\n");
            ToriDraw_ModelFree(m);
        }
    }

    /* EV_SAMPLE_IDS=a,b,c — the mean colour and saturation of named textures.
     * "The model is grey" has two causes that look identical: a grey texture,
     * or a colour that never reaches it. This tells them apart. */
    {
        const char* want = getenv("EV_SAMPLE_IDS");
        for( const char* q = want; q && *q; )
        {
            int tid = atoi(q);
            const struct EV_Texture* t = ev_textures_get(&set, tid);
            if( t )
            {
                long long r = 0, g = 0, b = 0;
                int n = t->size * t->size, maxsat = 0;
                for( int i = 0; i < n; i++ )
                {
                    int px = t->texels[i];
                    int rr = (px >> 16) & 0xFF, gg = (px >> 8) & 0xFF, bb = px & 0xFF;
                    int hi = rr > gg ? (rr > bb ? rr : bb) : (gg > bb ? gg : bb);
                    int lo = rr < gg ? (rr < bb ? rr : bb) : (gg < bb ? gg : bb);
                    if( hi - lo > maxsat ) maxsat = hi - lo;
                    r += rr; g += gg; b += bb;
                }
                printf("  texture %-5d mean=(%3lld,%3lld,%3lld) max chroma=%d %s\n",
                       tid, r/n, g/n, b/n, maxsat,
                       maxsat < 24 ? "<- essentially GREYSCALE" : "");
            }
            else printf("  texture %-5d absent\n", tid);
            /* EV_DUMP_TEX=dir — write the texture itself, because "the model
             * looks striped" and "the texture is striped" are different bugs. */
            if( t && getenv("EV_DUMP_TEX") )
            {
                char path[512];
                snprintf(path, sizeof(path), "%s/tex%d.bmp", getenv("EV_DUMP_TEX"), tid);
                FILE* f = fopen(path, "wb");
                if( f )
                {
                    int wpx = t->size, hpx = t->size;
                    int row = wpx * 3, pad = (4 - (row % 4)) % 4, dsz = (row + pad) * hpx;
                    unsigned char hdr[54] = { 'B', 'M' };
                    int fsz = 54 + dsz, off = 54, ih = 40;
                    memcpy(hdr + 2, &fsz, 4); memcpy(hdr + 10, &off, 4);
                    memcpy(hdr + 14, &ih, 4); memcpy(hdr + 18, &wpx, 4);
                    memcpy(hdr + 22, &hpx, 4); hdr[26] = 1; hdr[28] = 24;
                    memcpy(hdr + 34, &dsz, 4);
                    fwrite(hdr, 1, 54, f);
                    for( int y = hpx - 1; y >= 0; y-- )
                    {
                        for( int x = 0; x < wpx; x++ )
                        {
                            int px = t->texels[y * wpx + x];
                            unsigned char bgr[3] = { px & 0xFF, (px >> 8) & 0xFF, (px >> 16) & 0xFF };
                            fwrite(bgr, 1, 3, f);
                        }
                        unsigned char z[3] = { 0, 0, 0 };
                        fwrite(z, 1, (size_t)pad, f);
                    }
                    fclose(f);
                }
            }
            q = strchr(q, ','); if( q ) q++;
        }
    }

    ev_textures_free(&set);
    tool_dat2_close(&cache);
    return 0;
}
