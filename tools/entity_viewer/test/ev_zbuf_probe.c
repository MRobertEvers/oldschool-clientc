/*
 * ev_zbuf_probe — one-off: render an HD npc through ToriDraw_RenderHDZBuffered
 * (ev_set_zbuffer_kernels) at a sweep of yaws, one bmp per yaw, so the frames
 * can be eyeballed for texture/geometry decoupling as the camera orbits.
 *
 * tools/entity_viewer/ev_zbuf_probe cache.rs727_preeoc rs727 2745 --out /tmp/probe
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

static void
write_bmp(const char* path, const unsigned char* rgb, int w, int h)
{
    FILE* f = fopen(path, "wb");
    if( !f )
        return;
    int row_bytes = w * 3;
    int pad = (4 - (row_bytes % 4)) % 4;
    int data_size = (row_bytes + pad) * h;
    int file_size = 54 + data_size;
    unsigned char header[54] = { 0 };
    header[0] = 'B';
    header[1] = 'M';
    memcpy(&header[2], &file_size, 4);
    int offset = 54;
    memcpy(&header[10], &offset, 4);
    int hdr_size = 40;
    memcpy(&header[14], &hdr_size, 4);
    memcpy(&header[18], &w, 4);
    memcpy(&header[22], &h, 4);
    header[26] = 1;
    header[28] = 24;
    memcpy(&header[34], &data_size, 4);
    fwrite(header, 1, 54, f);
    unsigned char pad_bytes[4] = { 0 };
    for( int y = h - 1; y >= 0; y-- )
    {
        for( int x = 0; x < w; x++ )
        {
            const unsigned char* p = rgb + (y * w + x) * 3;
            unsigned char bgr[3] = { p[2], p[1], p[0] };
            fwrite(bgr, 1, 3, f);
        }
        if( pad )
            fwrite(pad_bytes, 1, (size_t)pad, f);
    }
    fclose(f);
}

extern uint8_t*
ev_wire_build_texture_blob(struct EV_TextureSet* set, size_t* out_len);

int
main(int argc, char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : "cache.rs727_preeoc";
    const char* rev = argc > 2 ? argv[2] : "rs727";
    int npc_id = argc > 3 ? atoi(argv[3]) : 2745;
    const char* out_prefix = "/tmp/ev_zbuf_probe";
    int tile = 384, pitch = 200;
    double frame = 2.6;
    int zbuf_kernels = 1;
    int yaw_start = 0, yaw_step = 100, yaw_count = 8;
    int zoom_override = 0;

    for( int i = 4; i < argc; i++ )
    {
        if( !strcmp(argv[i], "--out") && i + 1 < argc )
            out_prefix = argv[++i];
        else if( !strcmp(argv[i], "--tile") && i + 1 < argc )
            tile = atoi(argv[++i]);
        else if( !strcmp(argv[i], "--pitch") && i + 1 < argc )
            pitch = atoi(argv[++i]);
        else if( !strcmp(argv[i], "--frame") && i + 1 < argc )
            frame = atof(argv[++i]);
        else if( !strcmp(argv[i], "--sorted") )
            zbuf_kernels = 0;
        else if( !strcmp(argv[i], "--start") && i + 1 < argc )
            yaw_start = atoi(argv[++i]);
        else if( !strcmp(argv[i], "--step") && i + 1 < argc )
            yaw_step = atoi(argv[++i]);
        else if( !strcmp(argv[i], "--count") && i + 1 < argc )
            yaw_count = atoi(argv[++i]);
        else if( !strcmp(argv[i], "--zoom") && i + 1 < argc )
            zoom_override = atoi(argv[++i]);
    }

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
    if( !hd )
    {
        fprintf(stderr, "npc %d has no HD model\n", npc_id);
        return 1;
    }

    /* --coplanar: how far each type-0 face's P/M/N texture frame sits from the
     * face's own plane. Zero means the SD eye-ray plane walk and the HD
     * per-vertex projection agree; anything else and they diverge with view. */
    if( getenv("EV_COPLANAR") )
    {
        struct ToriDraw_Model* m = &hd->base;
        int n_type0 = 0, n_own = 0, n_off = 0;
        double worst = 0;
        int hist[6] = { 0 };
        for( int f = 0; f < m->face_count; f++ )
        {
            int coord = m->face_texture_coords ? m->face_texture_coords[f] : -1;
            if( coord < 0 || coord >= m->textured_face_count )
                continue;
            int ty = m->texture_render_types ? (m->texture_render_types[coord] & 0xFF) : 0;
            if( ty != 0 )
                continue;
            n_type0++;
            int ia = m->face_indices_a[f], ib = m->face_indices_b[f], ic = m->face_indices_c[f];
            int tp = m->textured_p_coordinate[coord], tm = m->textured_m_coordinate[coord],
                tn = m->textured_n_coordinate[coord];
            int own = 0;
            int idx[3] = { ia, ib, ic }, pmn[3] = { tp, tm, tn };
            for( int k = 0; k < 3; k++ )
                for( int j = 0; j < 3; j++ )
                    if( idx[k] == pmn[j] )
                        own++;
            if( own == 3 )
            {
                n_own++;
                continue;
            }
            /* face plane */
            double ax = m->vertices_x[ia], ay = m->vertices_y[ia], az = m->vertices_z[ia];
            double bx = m->vertices_x[ib] - ax, by = m->vertices_y[ib] - ay,
                   bz = m->vertices_z[ib] - az;
            double cx = m->vertices_x[ic] - ax, cy = m->vertices_y[ic] - ay,
                   cz = m->vertices_z[ic] - az;
            double nx = by * cz - bz * cy, ny = bz * cx - bx * cz, nz = bx * cy - by * cx;
            double nl = sqrt(nx * nx + ny * ny + nz * nz);
            if( nl == 0 )
                continue;
            double edge = sqrt(bx * bx + by * by + bz * bz);
            double dmax = 0;
            for( int j = 0; j < 3; j++ )
            {
                double px = m->vertices_x[pmn[j]] - ax, py = m->vertices_y[pmn[j]] - ay,
                       pz = m->vertices_z[pmn[j]] - az;
                double d = fabs((px * nx + py * ny + pz * nz) / nl);
                if( d > dmax )
                    dmax = d;
            }
            double rel = edge > 0 ? dmax / edge : 0;
            n_off++;
            if( rel > worst )
                worst = rel;
            int b = rel < 0.01 ? 0 : rel < 0.1 ? 1 : rel < 0.5 ? 2 : rel < 1 ? 3 : rel < 5 ? 4 : 5;
            hist[b]++;
        }
        printf(
            "type-0 faces %d: own-vertex PMN %d, foreign PMN %d\n"
            "  foreign PMN off-plane distance / face edge: <1%%: %d  <10%%: %d  <50%%: %d  <1x: %d  <5x: %d  >=5x: %d  worst %.1fx\n",
            n_type0, n_own, n_off, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], worst);
        return 0;
    }

    struct EV_WireBuf wb = { 0 };
    ev_wire_write_model_hd(&wb, hd);

    ev_init();
    int faces = ev_set_model_hd(wb.data, (int)wb.len);
    (void)faces;

    /* build the texture blob the same way ev_hd_sheet's build_blob does, via
     * the wire writer entry point ev_set_textures expects */
    extern uint8_t* ev_wire_write_textures(struct EV_TextureSet*, size_t*);
    size_t blob_len = 0;
    /* fall back: reuse ev_hd_sheet's approach isn't exported, so install
     * textures the same way ev_set_textures expects raw EVT1 bytes -- build
     * inline. */
    {
        int count = g_set.count;
        size_t cap = 16 + (size_t)count * (16 + 128 * 128 * 4);
        uint8_t* b = malloc(cap);
        size_t at = 0;
        memcpy(b + at, "EVT1", 4);
        at += 4;
        size_t count_at = at;
        at += 4;
        uint32_t written = 0;
        for( int i = 0; i < count; i++ )
        {
            struct EV_Texture* t = &g_set.items[i];
            if( !t->texels )
                continue;
            uint32_t id32 = (uint32_t)t->id;
            memcpy(b + at, &id32, 4);
            at += 4;
            b[at++] = (uint8_t)(t->size & 0xFF);
            b[at++] = (uint8_t)((t->size >> 8) & 0xFF);
            b[at++] = (uint8_t)(t->alpha_mode >= 0 && t->alpha_mode <= 2 ? t->alpha_mode : 0);
            b[at++] =
                (uint8_t)((t->repeat_s ? 0 : 1) | (t->repeat_t ? 0 : 2) | (t->modulate ? 4 : 0));
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
            written++;
        }
        memcpy(b + count_at, &written, 4);
        blob_len = at;
        int installed = ev_set_textures(b, (int)blob_len);
        printf(
            "%s npc %d: HD, %d faces, %d textures\n", cache_dir, npc_id, faces, installed);
        free(b);
    }

    ev_set_zbuffer_kernels(zbuf_kernels);

    int zoom = zoom_override ? zoom_override : (int)(ev_model_height() * frame);
    if( zoom < 260 )
        zoom = 260;

    int n_yaws = yaw_count;

    for( int i = 0; i < n_yaws; i++ )
    {
        int yaw = yaw_start + i * yaw_step;
        const unsigned char* rgba = ev_render(tile, tile, yaw, pitch, zoom, 0);
        if( !rgba )
        {
            fprintf(stderr, "render failed at yaw %d\n", yaw);
            continue;
        }
        unsigned char* rgb = malloc((size_t)tile * tile * 3);
        for( int p = 0; p < tile * tile; p++ )
        {
            rgb[p * 3 + 0] = rgba[p * 4 + 0];
            rgb[p * 3 + 1] = rgba[p * 4 + 1];
            rgb[p * 3 + 2] = rgba[p * 4 + 2];
        }
        char path[512];
        snprintf(path, sizeof(path), "%s_yaw%04d.bmp", out_prefix, yaw);
        write_bmp(path, rgb, tile, tile);
        printf("wrote %s\n", path);
        free(rgb);
    }

    ev_wire_free(&wb);
    ev_textures_free(&g_set);
    tool_dat2_close(&cache);
    return 0;
}
