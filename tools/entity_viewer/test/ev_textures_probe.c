#include "ev_textures.h"
#include "asset_access.h"
#include "tool_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include "ev_build.h"
#include "toridraw.h"

static int keep_all(int id, void* user) { (void)id; (void)user; return 1; }

int main(int argc, char** argv)
{
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    struct EV_TextureSet set;

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
            printf("  npc %-6d faces=%-5d textured=%-5d texfaces=%-4d rt[plane=%d cyl=%d cube=%d sph=%d] coords=%-5d distinct ids=%-3d in cache=%d ids:",
                   npc_id, m->face_count, textured, m->textured_face_count,
                   rt[0], rt[1], rt[2], rt[3], coords_ok, dn, present);
            for( int i = 0; i < dn && i < 10; i++ ) printf(" %d", distinct[i]);
            printf("\n");
            ToriDraw_ModelFree(m);
        }
    }

    ev_textures_free(&set);
    tool_dat2_close(&cache);
    return 0;
}
