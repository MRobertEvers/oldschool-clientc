/*
 * anim_compare — play one animation on two rigs, side by side, frame by frame.
 *
 * Retargeting an animation between cache eras is a question about *joints*, and
 * joints are invisible in every other view. A vertex label is an index into
 * whichever rig authored it, so the same number means a shoulder in one era and
 * a cape in another; nothing errors when the two disagree, the model simply
 * bends wrongly. The only reliable way to tell a good correspondence from a bad
 * one is to watch both play the same motion and see which frames diverge.
 *
 * Left panel is the source (a dat2 cache and a sequence id). Right panel may be
 * another dat2 cache (the foreign-cache import path) or the older directory of
 * `.ob2` body models plus a `.anim` animset. Frames are paired by index.
 *
 * Usage:
 *   anim_compare --a-rev osrs239 --a-cache cache.osrs239 --a-seq 7514
 *                --b-models <content>/models/human/man --b-anim <...>.anim
 *                --out /tmp/cmp [flags]
 *
 *   --a-model ID       pose this model instead of the player body, and
 *   --b-cache DIR --b-rev REV --b-seq ID --b-model ID
 *                      load the imported side from a baked dat2 cache
 *   --b-model FILE     the matching `.ob2` — for animations that are not player
 *                      animations, such as a spotanim rigged to its own framemap
 *   --frames LO-HI     only this frame range
 *   --size WxH         per-panel pixel size (default 320x400)
 *   --yaw N            turntable angle, 0..2047 (default 0, facing the camera)
 *   --scale N          model units per pixel x256 (default auto-fit)
 *   --by-label         colour faces by vertex label instead of by material.
 *                      This is the mode that finds rig bugs: matching joints
 *                      must be the same colour on both sides.
 *   --sheet            also write one contact sheet of every rendered frame
 *
 * Writes `frame_NNN.bmp` per frame plus `sheet.bmp`, into --out.
 */

#include "asset_access.h"
#include "tool_profile.h"

#include <assert.h>
#include <math.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


/*
 * The client's 2048-entry 16.16 sin/cos, rebuilt here rather than linked.
 *
 * Frame rotations are in units of 1/256 of a turn scaled onto this table, which
 * is the convention the animation kernel below is a port of — using anything
 * else would move joints by the wrong amount and defeat the comparison.
 */
static int g_sin[2048];
static int g_cos[2048];

static void
trig_init(void)
{
    for( int i = 0; i < 2048; i++ )
    {
        g_sin[i] = (int)(sin((double)i * 0.0030679615) * 65536.0);
        g_cos[i] = (int)(cos((double)i * 0.0030679615) * 65536.0);
    }
}

/* ---- a rig, loaded from either era ------------------------------------- */

struct Frame
{
    int count;
    int* group;
    int* dx;
    int* dy;
    int* dz;
};

struct Rig
{
    char name[64];

    int vertex_count;
    int* ox;
    int* oy;
    int* oz; /* rest pose */
    int* vx;
    int* vy;
    int* vz; /* working */
    uint8_t* vlabel;

    int face_count;
    int* fa;
    int* fb;
    int* fc;
    int* fcolour; /* rgb888 */

    /* rig */
    int transform_count;
    uint8_t* ttype;
    uint8_t** tlabels;
    int* tlabel_count;

    /* label -> vertex indices */
    int label_max;
    int** label_verts;
    int* label_vert_count;

    struct Frame* frames;
    int frame_count;
};

/* HSL16 -> RGB888, the palette the caches store colours in. */
static int
hsl_to_rgb(int hsl)
{
    double h = ((hsl >> 10) & 0x3f) / 64.0;
    double s = ((hsl >> 7) & 0x7) / 8.0;
    double l = (hsl & 0x7f) / 128.0;
    double r, g, b, q, p, t[3], v;
    int out[3];
    if( s <= 0.0 )
    {
        r = g = b = l;
        out[0] = (int)(r * 255);
        out[1] = (int)(g * 255);
        out[2] = (int)(b * 255);
        return (out[0] << 16) | (out[1] << 8) | out[2];
    }
    q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    p = 2.0 * l - q;
    t[0] = h + 1.0 / 3.0;
    t[1] = h;
    t[2] = h - 1.0 / 3.0;
    for( int i = 0; i < 3; i++ )
    {
        double c = t[i];
        if( c < 0 )
            c += 1;
        if( c > 1 )
            c -= 1;
        if( c < 1.0 / 6.0 )
            v = p + (q - p) * 6.0 * c;
        else if( c < 0.5 )
            v = q;
        else if( c < 2.0 / 3.0 )
            v = p + (q - p) * (2.0 / 3.0 - c) * 6.0;
        else
            v = p;
        out[i] = (int)(v * 255);
        if( out[i] < 0 )
            out[i] = 0;
        if( out[i] > 255 )
            out[i] = 255;
    }
    return (out[0] << 16) | (out[1] << 8) | out[2];
}

/* A stable, well-spread colour per label, so the same joint reads the same on
 * both panels regardless of what the two eras call it. */
static int
label_colour(int label)
{
    static const int table[] = { 0xe6194b, 0x3cb44b, 0xffe119, 0x4363d8, 0xf58231,
                                 0x911eb4, 0x46f0f0, 0xf032e6, 0xbcf60c, 0xfabebe,
                                 0x008080, 0xe6beff, 0x9a6324, 0xfffac8, 0x800000,
                                 0xaaffc3, 0x808000, 0xffd8b1, 0x000075, 0xa9a9a9 };
    if( label == 255 )
        return 0x303030;
    return table[label % (int)(sizeof(table) / sizeof(table[0]))];
}

/* ---- geometry accumulation --------------------------------------------- */

struct Builder
{
    int vcap, fcap;
    struct Rig* rig;
};

static void
rig_add_model(
    struct Rig* rig,
    const struct RSCache_Model* m)
{
    int base = rig->vertex_count;
    int nv = rig->vertex_count + m->vertex_count;
    int nf = rig->face_count + m->face_count;

    rig->ox = realloc(rig->ox, (size_t)nv * sizeof(int));
    rig->oy = realloc(rig->oy, (size_t)nv * sizeof(int));
    rig->oz = realloc(rig->oz, (size_t)nv * sizeof(int));
    rig->vlabel = realloc(rig->vlabel, (size_t)nv);
    rig->fa = realloc(rig->fa, (size_t)nf * sizeof(int));
    rig->fb = realloc(rig->fb, (size_t)nf * sizeof(int));
    rig->fc = realloc(rig->fc, (size_t)nf * sizeof(int));
    rig->fcolour = realloc(rig->fcolour, (size_t)nf * sizeof(int));

    for( int i = 0; i < m->vertex_count; i++ )
    {
        rig->ox[base + i] = m->vertices_x[i];
        rig->oy[base + i] = m->vertices_y[i];
        rig->oz[base + i] = m->vertices_z[i];
        rig->vlabel[base + i] = m->vertex_bone_map ? m->vertex_bone_map[i] : 255;
    }
    for( int i = 0; i < m->face_count; i++ )
    {
        int f = rig->face_count + i;
        rig->fa[f] = base + m->face_indices_a[i];
        rig->fb[f] = base + m->face_indices_b[i];
        rig->fc[f] = base + m->face_indices_c[i];
        rig->fcolour[f] = hsl_to_rgb(m->face_colors ? m->face_colors[i] : 0);
    }
    rig->vertex_count = nv;
    rig->face_count = nf;
}

static void
rig_finish_geometry(struct Rig* rig)
{
    int max = 0;
    rig->vx = malloc((size_t)rig->vertex_count * sizeof(int));
    rig->vy = malloc((size_t)rig->vertex_count * sizeof(int));
    rig->vz = malloc((size_t)rig->vertex_count * sizeof(int));
    for( int i = 0; i < rig->vertex_count; i++ )
        if( rig->vlabel[i] != 255 && rig->vlabel[i] > max )
            max = rig->vlabel[i];
    rig->label_max = max;
    rig->label_verts = calloc((size_t)max + 1, sizeof(int*));
    rig->label_vert_count = calloc((size_t)max + 1, sizeof(int));
    for( int i = 0; i < rig->vertex_count; i++ )
        if( rig->vlabel[i] != 255 )
            rig->label_vert_count[rig->vlabel[i]]++;
    for( int l = 0; l <= max; l++ )
        if( rig->label_vert_count[l] )
        {
            rig->label_verts[l] = malloc((size_t)rig->label_vert_count[l] * sizeof(int));
            rig->label_vert_count[l] = 0;
        }
    for( int i = 0; i < rig->vertex_count; i++ )
    {
        int l = rig->vlabel[i];
        if( l == 255 )
            continue;
        rig->label_verts[l][rig->label_vert_count[l]++] = i;
    }
}

/* ---- the animation kernel ---------------------------------------------- */
/*
 * A line-for-line port of the client's Model.animate2 (Client-TS
 * src/dash3d/Model.ts:1237, identical in xrsps). It has to be the reference
 * behaviour and not an approximation, including the part that causes the bug
 * this tool exists to find: an ORIGIN whose labels match nothing does not skip,
 * it falls back to using the raw frame value as an absolute pivot.
 */
static int g_ox, g_oy, g_oz;

static void
rig_transform(
    struct Rig* rig,
    int type,
    const uint8_t* labels,
    int label_count,
    int x,
    int y,
    int z)
{
    if( type == 0 ) /* ORIGIN */
    {
        int count = 0;
        g_ox = g_oy = g_oz = 0;
        for( int g = 0; g < label_count; g++ )
        {
            int l = labels[g];
            if( l > rig->label_max || !rig->label_verts[l] )
                continue;
            for( int i = 0; i < rig->label_vert_count[l]; i++ )
            {
                int v = rig->label_verts[l][i];
                g_ox += rig->vx[v];
                g_oy += rig->vy[v];
                g_oz += rig->vz[v];
                count++;
            }
        }
        if( count > 0 )
        {
            g_ox = g_ox / count + x;
            g_oy = g_oy / count + y;
            g_oz = g_oz / count + z;
        }
        else
        {
            g_ox = x;
            g_oy = y;
            g_oz = z;
        }
    }
    else if( type == 1 ) /* TRANSLATE */
    {
        for( int g = 0; g < label_count; g++ )
        {
            int l = labels[g];
            if( l > rig->label_max || !rig->label_verts[l] )
                continue;
            for( int i = 0; i < rig->label_vert_count[l]; i++ )
            {
                int v = rig->label_verts[l][i];
                rig->vx[v] += x;
                rig->vy[v] += y;
                rig->vz[v] += z;
            }
        }
    }
    else if( type == 2 ) /* ROTATE */
    {
        /* Order matters and is not obvious: the reference applies roll (z),
         * then pitch (x), then yaw (y). Rotations do not commute, so doing them
         * in index order instead produces a pose that is wrong in a way that
         * looks like a bad rig. */
        for( int g = 0; g < label_count; g++ )
        {
            int l = labels[g];
            if( l > rig->label_max || !rig->label_verts[l] )
                continue;
            for( int i = 0; i < rig->label_vert_count[l]; i++ )
            {
                int v = rig->label_verts[l][i];
                int pitch = (x & 0xff) * 8;
                int yaw = (y & 0xff) * 8;
                int roll = (z & 0xff) * 8;
                int sin, cos, t;

                rig->vx[v] -= g_ox;
                rig->vy[v] -= g_oy;
                rig->vz[v] -= g_oz;

                if( roll != 0 )
                {
                    sin = g_sin[roll];
                    cos = g_cos[roll];
                    t = (rig->vy[v] * sin + rig->vx[v] * cos) >> 16;
                    rig->vy[v] = (rig->vy[v] * cos - rig->vx[v] * sin) >> 16;
                    rig->vx[v] = t;
                }
                if( pitch != 0 )
                {
                    sin = g_sin[pitch];
                    cos = g_cos[pitch];
                    t = (rig->vy[v] * cos - rig->vz[v] * sin) >> 16;
                    rig->vz[v] = (rig->vy[v] * sin + rig->vz[v] * cos) >> 16;
                    rig->vy[v] = t;
                }
                if( yaw != 0 )
                {
                    sin = g_sin[yaw];
                    cos = g_cos[yaw];
                    t = (rig->vz[v] * sin + rig->vx[v] * cos) >> 16;
                    rig->vz[v] = (rig->vz[v] * cos - rig->vx[v] * sin) >> 16;
                    rig->vx[v] = t;
                }

                rig->vx[v] += g_ox;
                rig->vy[v] += g_oy;
                rig->vz[v] += g_oz;
            }
        }
    }
    else if( type == 3 ) /* SCALE */
    {
        for( int g = 0; g < label_count; g++ )
        {
            int l = labels[g];
            if( l > rig->label_max || !rig->label_verts[l] )
                continue;
            for( int i = 0; i < rig->label_vert_count[l]; i++ )
            {
                int v = rig->label_verts[l][i];
                rig->vx[v] = g_ox + ((rig->vx[v] - g_ox) * x) / 128;
                rig->vy[v] = g_oy + ((rig->vy[v] - g_oy) * y) / 128;
                rig->vz[v] = g_oz + ((rig->vz[v] - g_oz) * z) / 128;
            }
        }
    }
    /* type 5 ALPHA changes transparency only; nothing to do for geometry. */
}

static void
rig_pose(
    struct Rig* rig,
    int frame_index)
{
    memcpy(rig->vx, rig->ox, (size_t)rig->vertex_count * sizeof(int));
    memcpy(rig->vy, rig->oy, (size_t)rig->vertex_count * sizeof(int));
    memcpy(rig->vz, rig->oz, (size_t)rig->vertex_count * sizeof(int));
    if( frame_index < 0 || frame_index >= rig->frame_count )
        return;
    struct Frame* f = &rig->frames[frame_index];
    for( int i = 0; i < f->count; i++ )
    {
        int t = f->group[i];
        if( t < 0 || t >= rig->transform_count )
            continue;
        rig_transform(
            rig, rig->ttype[t], rig->tlabels[t], rig->tlabel_count[t], f->dx[i], f->dy[i], f->dz[i]);
    }
}

/* ---- rasteriser --------------------------------------------------------- */

struct Canvas
{
    int w, h;
    unsigned char* px; /* rgb */
};

static void
canvas_fill(
    struct Canvas* c,
    int rgb)
{
    for( int i = 0; i < c->w * c->h; i++ )
    {
        c->px[i * 3 + 0] = (rgb >> 16) & 0xff;
        c->px[i * 3 + 1] = (rgb >> 8) & 0xff;
        c->px[i * 3 + 2] = rgb & 0xff;
    }
}

static void
tri(
    struct Canvas* c,
    int x0,
    int y0,
    int x1,
    int y1,
    int x2,
    int y2,
    int rgb)
{
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if( minx < 0 )
        minx = 0;
    if( miny < 0 )
        miny = 0;
    if( maxx >= c->w )
        maxx = c->w - 1;
    if( maxy >= c->h )
        maxy = c->h - 1;
    int area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if( area == 0 )
        return;
    for( int y = miny; y <= maxy; y++ )
        for( int x = minx; x <= maxx; x++ )
        {
            int w0 = (x1 - x0) * (y - y0) - (x - x0) * (y1 - y0);
            int w1 = (x2 - x1) * (y - y1) - (x - x1) * (y2 - y1);
            int w2 = (x0 - x2) * (y - y2) - (x - x2) * (y0 - y2);
            int inside = area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                                  : (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if( !inside )
                continue;
            int o = (y * c->w + x) * 3;
            c->px[o + 0] = (rgb >> 16) & 0xff;
            c->px[o + 1] = (rgb >> 8) & 0xff;
            c->px[o + 2] = rgb & 0xff;
        }
}

struct FaceOrder
{
    int face;
    int depth;
};

static int
face_cmp(
    const void* a,
    const void* b)
{
    return ((const struct FaceOrder*)b)->depth - ((const struct FaceOrder*)a)->depth;
}

/*
 * Orthographic, y-up-is-negative as the caches store it, painter-sorted.
 *
 * No lighting and no texture: this is a diagnostic view, and flat colour makes a
 * limb hinging on the wrong joint obvious where shading would hide it.
 */
static void
rig_render(
    struct Rig* rig,
    struct Canvas* c,
    int yaw,
    int scale_256,
    int by_label)
{
    struct FaceOrder* order = malloc((size_t)rig->face_count * sizeof(*order));
    int sin = g_sin[yaw & 0x7ff];
    int cos = g_cos[yaw & 0x7ff];
    int* sx = malloc((size_t)rig->vertex_count * sizeof(int));
    int* sy = malloc((size_t)rig->vertex_count * sizeof(int));
    int* sd = malloc((size_t)rig->vertex_count * sizeof(int));

    for( int i = 0; i < rig->vertex_count; i++ )
    {
        int x = (rig->vx[i] * cos - rig->vz[i] * sin) >> 16;
        int z = (rig->vx[i] * sin + rig->vz[i] * cos) >> 16;
        sx[i] = c->w / 2 + (x * scale_256 >> 8);
        sy[i] = (c->h * 4) / 5 + (rig->vy[i] * scale_256 >> 8);
        sd[i] = z;
    }
    for( int f = 0; f < rig->face_count; f++ )
    {
        order[f].face = f;
        order[f].depth = (sd[rig->fa[f]] + sd[rig->fb[f]] + sd[rig->fc[f]]) / 3;
    }
    qsort(order, (size_t)rig->face_count, sizeof(*order), face_cmp);
    for( int i = 0; i < rig->face_count; i++ )
    {
        int f = order[i].face;
        int rgb = by_label ? label_colour(rig->vlabel[rig->fa[f]]) : rig->fcolour[f];
        tri(c,
            sx[rig->fa[f]],
            sy[rig->fa[f]],
            sx[rig->fb[f]],
            sy[rig->fb[f]],
            sx[rig->fc[f]],
            sy[rig->fc[f]],
            rgb);
    }
    free(order);
    free(sx);
    free(sy);
    free(sd);
}

static void
bmp_write(
    const char* path,
    const struct Canvas* c)
{
    int stride = (c->w * 3 + 3) & ~3;
    int size = 54 + stride * c->h;
    unsigned char h[54] = { 0 };
    FILE* f = fopen(path, "wb");
    if( !f )
        return;
    h[0] = 'B';
    h[1] = 'M';
    h[2] = size & 0xff;
    h[3] = (size >> 8) & 0xff;
    h[4] = (size >> 16) & 0xff;
    h[5] = (size >> 24) & 0xff;
    h[10] = 54;
    h[14] = 40;
    h[18] = c->w & 0xff;
    h[19] = (c->w >> 8) & 0xff;
    h[20] = (c->w >> 16) & 0xff;
    h[22] = c->h & 0xff;
    h[23] = (c->h >> 8) & 0xff;
    h[24] = (c->h >> 16) & 0xff;
    h[26] = 1;
    h[28] = 24;
    fwrite(h, 1, 54, f);
    unsigned char* row = calloc(1, (size_t)stride);
    for( int y = c->h - 1; y >= 0; y-- )
    {
        for( int x = 0; x < c->w; x++ )
        {
            row[x * 3 + 0] = c->px[(y * c->w + x) * 3 + 2];
            row[x * 3 + 1] = c->px[(y * c->w + x) * 3 + 1];
            row[x * 3 + 2] = c->px[(y * c->w + x) * 3 + 0];
        }
        fwrite(row, 1, (size_t)stride, f);
    }
    free(row);
    fclose(f);
}

static void
blit(
    struct Canvas* dst,
    const struct Canvas* src,
    int ox,
    int oy)
{
    for( int y = 0; y < src->h; y++ )
    {
        int dy = oy + y;
        if( dy < 0 || dy >= dst->h )
            continue;
        for( int x = 0; x < src->w; x++ )
        {
            int dx = ox + x;
            if( dx < 0 || dx >= dst->w )
                continue;
            memcpy(&dst->px[(dy * dst->w + dx) * 3], &src->px[(y * src->w + x) * 3], 3);
        }
    }
}

/* ---- loading: dat2 source side ------------------------------------------ */

static int
load_side_dat2(
    struct Rig* rig,
    const char* rev,
    const char* dir,
    int seq_id,
    int model_id)
{
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    struct RSCache_Dat2ConfigSequence* seq;
    struct RSCache_Dat2Framemap* fm;
    int* ids = NULL;
    int count = 0;
    int framemap_id;

    if( !tool_resolve_profile(rev, NULL, NULL, NULL, NULL, &profile) )
        return 0;
    if( !tool_dat2_open(dir, &profile, &cache) )
    {
        fprintf(stderr, "anim_compare: cannot open dat2 cache %s\n", dir);
        return 0;
    }
    seq = tool_dat2_seq_load(&cache, seq_id);
    if( !seq )
    {
        fprintf(stderr, "anim_compare: seq %d absent\n", seq_id);
        return 0;
    }
    framemap_id = tool_dat2_seq_framemap_id(&cache, seq, 1);
    fm = tool_dat2_framemap_load(&cache, framemap_id);
    if( !fm )
    {
        fprintf(stderr, "anim_compare: framemap %d absent\n", framemap_id);
        return 0;
    }

    /* An explicit model when the animation is not a player one — a spotanim is
     * rigged to its own framemap and its own mesh, and posing the player body
     * with it would compare two unrelated things. */
    if( model_id >= 0 )
    {
        struct RSCache_Model* m = tool_dat2_model_load(&cache, model_id);
        if( !m )
        {
            fprintf(stderr, "anim_compare: model %d absent\n", model_id);
            return 0;
        }
        rig_add_model(rig, m);
        RSCache_ModelFree(m);
    }
    else if( tool_dat2_idk_models(&cache, &ids, &count) )
    {
        for( int i = 0; i < count; i++ )
        {
            struct RSCache_Model* m = tool_dat2_model_load(&cache, ids[i]);
            if( !m )
                continue;
            if( m->vertex_bone_map )
                rig_add_model(rig, m);
            RSCache_ModelFree(m);
        }
        free(ids);
    }
    rig_finish_geometry(rig);

    rig->transform_count = fm->length;
    rig->ttype = malloc((size_t)fm->length);
    rig->tlabels = calloc((size_t)fm->length, sizeof(uint8_t*));
    rig->tlabel_count = calloc((size_t)fm->length, sizeof(int));
    for( int i = 0; i < fm->length; i++ )
    {
        rig->ttype[i] = (uint8_t)fm->types[i];
        rig->tlabel_count[i] = fm->bone_groups_lengths[i];
        rig->tlabels[i] = malloc((size_t)fm->bone_groups_lengths[i] + 1);
        for( int j = 0; j < fm->bone_groups_lengths[i]; j++ )
            rig->tlabels[i][j] = (uint8_t)fm->bone_groups[i][j];
    }

    rig->frames = calloc((size_t)seq->frame_count, sizeof(struct Frame));
    rig->frame_count = 0;
    for( int i = 0; i < seq->frame_count; i++ )
    {
        /* A sequence's frame id is a composite (archive << 16 | file), not an
         * archive id — handing it over whole asks the cache for archive
         * 707788820 and gets nothing. */
        int composite = seq->frame_ids[i];
        int arch = (composite >> 16) & 0xFFFF;
        int file_id = composite & 0xFFFF;
        int anim_table = RSCache_Dat2DiskTableId(cache.disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(cache.disk, anim_table, arch);
        struct RSCache_FileList* files;
        struct RSCache_Dat2Frame* fr = NULL;
        struct Frame* out;
        int pos;

        if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(cache.disk, archive) )
        {
            if( archive )
                RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( !files )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        pos = tool_archive_file_position(archive, file_id);
        if( pos >= 0 && pos < files->file_count && files->file_sizes[pos] > 0 )
            fr = RSCache_Dat2FrameNewDecodeProfile(
                &cache.profile, file_id, fm, files->files[pos], files->file_sizes[pos]);
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !fr )
            continue;
        out = &rig->frames[rig->frame_count++];
        out->count = fr->translator_count;
        out->group = malloc((size_t)out->count * sizeof(int));
        out->dx = malloc((size_t)out->count * sizeof(int));
        out->dy = malloc((size_t)out->count * sizeof(int));
        out->dz = malloc((size_t)out->count * sizeof(int));
        for( int j = 0; j < out->count; j++ )
        {
            out->group[j] = fr->index_frame_ids[j];
            out->dx[j] = fr->translator_arg_x[j];
            out->dy[j] = fr->translator_arg_y[j];
            out->dz[j] = fr->translator_arg_z[j];
        }
        RSCache_Dat2FrameFree(fr);
    }
    snprintf(rig->name, sizeof(rig->name), "%s seq %d", rev, seq_id);
    return rig->frame_count > 0;
}

/* ---- loading: dat1 ported side ------------------------------------------ */

static char*
slurp(
    const char* path,
    long* size)
{
    FILE* f = fopen(path, "rb");
    char* d;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = malloc((size_t)*size);
    if( !d || fread(d, 1, (size_t)*size, f) != (size_t)*size )
    {
        free(d);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return d;
}

/*
 * Walk a directory tree collecting `.ob2` models, one per directory.
 *
 * One per directory is the point: `models/human/man` holds every *variant* of
 * each body part — a dozen heads, a dozen torsos — and a player wears one of
 * each, not all of them. Merging the lot stacks a dozen overlapping meshes and
 * the result reads as an exploded figure whether or not the rig is right, which
 * makes the comparison useless. This mirrors the one-kit-per-body-part rule the
 * dat2 side gets from tool_dat2_idk_models.
 */
static void
add_ob2_tree(
    struct Rig* rig,
    const char* dir)
{
    int taken = 0;
    DIR* d = opendir(dir);
    struct dirent* e;
    if( !d )
        return;
    while( (e = readdir(d)) )
    {
        char path[1024];
        struct stat st;
        if( e->d_name[0] == '.' )
            continue;
        /* chathead and jaw are a separate head-only rig that reuses low label
         * numbers with different meanings; including them would draw a second,
         * wrongly-posed head over the body. */
        if( strcmp(e->d_name, "chathead") == 0 || strcmp(e->d_name, "jaw") == 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if( stat(path, &st) != 0 )
            continue;
        if( S_ISDIR(st.st_mode) )
        {
            add_ob2_tree(rig, path);
            continue;
        }
        if( strstr(e->d_name, ".ob2") )
        {
            if( taken )
                continue;
            long size;
            char* bytes = slurp(path, &size);
            struct RSCache_Model* m;
            if( !bytes )
                continue;
            m = RSCache_ModelNewDecode(bytes, (int)size);
            if( m && m->vertex_bone_map )
            {
                rig_add_model(rig, m);
                taken = 1;
            }
            if( m )
                RSCache_ModelFree(m);
            free(bytes);
        }
    }
    closedir(d);
}

static int
load_side_dat1(
    struct Rig* rig,
    const char* models_dir,
    const char* model_file,
    const char* anim_path)
{
    long size;
    char* bytes = slurp(anim_path, &size);
    struct RSCache_Dat1AnimBaseFrames* abf;

    if( !bytes )
    {
        fprintf(stderr, "anim_compare: cannot read %s\n", anim_path);
        return 0;
    }
    abf = RSCache_Dat1AnimBaseFramesNewDecode(bytes, (int)size);
    free(bytes);
    if( !abf || !abf->base )
    {
        fprintf(stderr, "anim_compare: %s is not an animset\n", anim_path);
        return 0;
    }

    if( model_file )
    {
        long msize;
        char* mbytes = slurp(model_file, &msize);
        struct RSCache_Model* m = mbytes ? RSCache_ModelNewDecode(mbytes, (int)msize) : NULL;
        if( !m )
        {
            fprintf(stderr, "anim_compare: cannot read model %s\n", model_file);
            free(mbytes);
            return 0;
        }
        rig_add_model(rig, m);
        RSCache_ModelFree(m);
        free(mbytes);
    }
    else
        add_ob2_tree(rig, models_dir);
    rig_finish_geometry(rig);

    rig->transform_count = abf->base->length;
    rig->ttype = malloc((size_t)abf->base->length);
    rig->tlabels = calloc((size_t)abf->base->length, sizeof(uint8_t*));
    rig->tlabel_count = calloc((size_t)abf->base->length, sizeof(int));
    for( int i = 0; i < abf->base->length; i++ )
    {
        rig->ttype[i] = abf->base->types[i];
        rig->tlabel_count[i] = abf->base->label_counts[i];
        rig->tlabels[i] = malloc((size_t)abf->base->label_counts[i] + 1);
        memcpy(rig->tlabels[i], abf->base->labels[i], (size_t)abf->base->label_counts[i]);
    }

    rig->frames = calloc((size_t)abf->frame_count, sizeof(struct Frame));
    rig->frame_count = abf->frame_count;
    for( int i = 0; i < abf->frame_count; i++ )
    {
        struct RSCache_Dat1AnimFrame* fr = &abf->frames[i];
        struct Frame* out = &rig->frames[i];
        out->count = fr->length;
        out->group = malloc((size_t)out->count * sizeof(int));
        out->dx = malloc((size_t)out->count * sizeof(int));
        out->dy = malloc((size_t)out->count * sizeof(int));
        out->dz = malloc((size_t)out->count * sizeof(int));
        for( int j = 0; j < out->count; j++ )
        {
            out->group[j] = fr->groups[j];
            out->dx[j] = fr->x[j];
            out->dy[j] = fr->y[j];
            out->dz[j] = fr->z[j];
        }
    }
    snprintf(rig->name, sizeof(rig->name), "port %s", strrchr(anim_path, '/')
                                                          ? strrchr(anim_path, '/') + 1
                                                          : anim_path);
    return rig->frame_count > 0;
}


/*
 * Which transforms the frames actually drive.
 *
 * A framemap can carry hundreds of transforms while a given animation touches a
 * few dozen. Only the touched ones matter for a retarget, so this narrows "fix
 * the whole rig" down to "fix these joints" — and printed side by side, a joint
 * that mapped to the wrong counterpart is visible as a mismatch in the label
 * columns rather than as a vaguely wrong pose.
 */
static void
report_used(
    struct Rig* a,
    struct Rig* b)
{
    unsigned char used_a[4096] = { 0 };
    unsigned char used_b[4096] = { 0 };
    int n = a->transform_count < b->transform_count ? a->transform_count : b->transform_count;

    for( int f = 0; f < a->frame_count; f++ )
        for( int i = 0; i < a->frames[f].count; i++ )
            if( a->frames[f].group[i] >= 0 && a->frames[f].group[i] < 4096 )
                used_a[a->frames[f].group[i]] = 1;
    for( int f = 0; f < b->frame_count; f++ )
        for( int i = 0; i < b->frames[f].count; i++ )
            if( b->frames[f].group[i] >= 0 && b->frames[f].group[i] < 4096 )
                used_b[b->frames[f].group[i]] = 1;

    printf("\n%-5s %-4s | %-34s | %s\n", "xform", "type", "source labels", "port labels (live only)");
    for( int t = 0; t < n; t++ )
    {
        int live = 0;
        if( !used_a[t] && !used_b[t] )
            continue;
        printf("%-5d %-4d | ", t, a->ttype[t]);
        for( int j = 0; j < a->tlabel_count[t] && j < 10; j++ )
            printf("%d ", a->tlabels[t][j]);
        if( a->tlabel_count[t] > 10 )
            printf("+%d ", a->tlabel_count[t] - 10);
        printf("%*s| ", (int)(34 - 0), "");
        for( int j = 0; j < b->tlabel_count[t]; j++ )
            if( b->tlabels[t][j] <= b->label_max && b->label_verts[b->tlabels[t][j]] )
            {
                printf("%d ", b->tlabels[t][j]);
                live++;
            }
        if( !live )
            printf("(none)");
        printf("\n");
    }
}


/*
 * How far each joint actually travels, per side.
 *
 * Eyeballing two different meshes only goes so far: the bodies differ, so a
 * pose can look wrong when it is right and right when it is wrong. What is
 * comparable is *motion* — how far a joint's vertices move from the rest pose,
 * averaged over the animation, expressed as a fraction of body height so the
 * two meshes' scales cancel.
 *
 * Read it against the rig map: a joint that travels in the source but barely
 * moves in the port is underdriven (its counterpart is unmapped or mapped to a
 * stub); one that travels much further is overdriven (several source joints
 * collapsed onto it without deduplication).
 */
static void
report_motion(
    struct Rig* r,
    const char* tag)
{
    double sum[256] = { 0 };
    long long n[256] = { 0 };
    int lo = 0, hi = 0;

    for( int i = 0; i < r->vertex_count; i++ )
    {
        if( r->oy[i] < lo )
            lo = r->oy[i];
        if( r->oy[i] > hi )
            hi = r->oy[i];
    }
    double height = (hi - lo) > 0 ? (double)(hi - lo) : 1.0;

    for( int f = 0; f < r->frame_count; f++ )
    {
        rig_pose(r, f);
        for( int i = 0; i < r->vertex_count; i++ )
        {
            int l = r->vlabel[i];
            double dx, dy, dz;
            if( l == 255 )
                continue;
            dx = r->vx[i] - r->ox[i];
            dy = r->vy[i] - r->oy[i];
            dz = r->vz[i] - r->oz[i];
            sum[l] += sqrt(dx * dx + dy * dy + dz * dz);
            n[l]++;
        }
    }
    printf("\n%s — mean joint travel, %% of body height\n", tag);
    for( int l = 0; l < 256; l++ )
        if( n[l] )
            printf("  joint %3d  n=%-6lld  %5.1f%%\n", l, n[l] / (long long)r->frame_count,
                   100.0 * (sum[l] / (double)n[l]) / height);
}

/* ---- fit --------------------------------------------------------------- */

/* Scale so the rest pose fills the panel, computed once from both rigs so the
 * two panels are directly comparable rather than each self-normalised. */
static int
auto_scale(
    struct Rig* a,
    struct Rig* b,
    int panel_h)
{
    int lo = 0, hi = 0;
    struct Rig* r[2] = { a, b };
    for( int k = 0; k < 2; k++ )
        for( int i = 0; i < r[k]->vertex_count; i++ )
        {
            if( r[k]->oy[i] < lo )
                lo = r[k]->oy[i];
            if( r[k]->oy[i] > hi )
                hi = r[k]->oy[i];
        }
    if( hi - lo <= 0 )
        return 256;
    return (panel_h * 3 / 4) * 256 / (hi - lo);
}

int
main(int argc, char** argv)
{
    const char* a_rev = NULL;
    const char* a_cache = NULL;
    const char* b_rev = NULL;
    const char* b_cache = NULL;
    const char* b_models = NULL;
    const char* b_anim = NULL;
    const char* out = "anim_compare_out";
    const char* b_model_arg = NULL;
    int a_model = -1;
    int a_seq = -1, b_seq = -1, lo = 0, hi = -1, pw = 320, ph = 400, yaw = 0, scale = 0;
    int by_label = 0, sheet = 0, report = 0, motion = 0;
    struct Rig ra, rb;

    trig_init();
    memset(&ra, 0, sizeof(ra));
    memset(&rb, 0, sizeof(rb));

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--a-rev") == 0 && i + 1 < argc )
            a_rev = argv[++i];
        else if( strcmp(argv[i], "--a-cache") == 0 && i + 1 < argc )
            a_cache = argv[++i];
        else if( strcmp(argv[i], "--a-seq") == 0 && i + 1 < argc )
            a_seq = atoi(argv[++i]);
        else if( strcmp(argv[i], "--b-rev") == 0 && i + 1 < argc )
            b_rev = argv[++i];
        else if( strcmp(argv[i], "--b-cache") == 0 && i + 1 < argc )
            b_cache = argv[++i];
        else if( strcmp(argv[i], "--b-seq") == 0 && i + 1 < argc )
            b_seq = atoi(argv[++i]);
        else if( strcmp(argv[i], "--b-models") == 0 && i + 1 < argc )
            b_models = argv[++i];
        else if( strcmp(argv[i], "--a-model") == 0 && i + 1 < argc )
            a_model = atoi(argv[++i]);
        else if( strcmp(argv[i], "--b-model") == 0 && i + 1 < argc )
            b_model_arg = argv[++i];
        else if( strcmp(argv[i], "--b-anim") == 0 && i + 1 < argc )
            b_anim = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out = argv[++i];
        else if( strcmp(argv[i], "--frames") == 0 && i + 1 < argc )
            sscanf(argv[++i], "%d-%d", &lo, &hi);
        else if( strcmp(argv[i], "--size") == 0 && i + 1 < argc )
            sscanf(argv[++i], "%dx%d", &pw, &ph);
        else if( strcmp(argv[i], "--yaw") == 0 && i + 1 < argc )
            yaw = atoi(argv[++i]);
        else if( strcmp(argv[i], "--scale") == 0 && i + 1 < argc )
            scale = atoi(argv[++i]);
        else if( strcmp(argv[i], "--by-label") == 0 )
            by_label = 1;
        else if( strcmp(argv[i], "--sheet") == 0 )
            sheet = 1;
        else if( strcmp(argv[i], "--report") == 0 )
            report = 1;
        else if( strcmp(argv[i], "--motion") == 0 )
            motion = 1;
        else
        {
            fprintf(stderr, "anim_compare: unknown option %s\n", argv[i]);
            return 2;
        }
    }
    int b_dat2 = b_cache || b_rev || b_seq >= 0;
    int b_dat1 = b_models || b_anim || (b_model_arg && !b_dat2);
    if( !a_rev || !a_cache || a_seq < 0 || b_dat2 == b_dat1 ||
        (b_dat2 && (!b_cache || !b_rev || b_seq < 0)) ||
        (b_dat1 && ((!b_models && !b_model_arg) || !b_anim)) )
    {
        fprintf(stderr,
                "Usage: anim_compare --a-rev REV --a-cache DIR --a-seq ID\n"
                "                    (--b-cache DIR --b-rev REV --b-seq ID [--b-model ID])\n"
                "                 or (--b-models DIR | --b-model FILE.ob2) --b-anim FILE.anim\n"
                "                    [--a-model ID   compare one model, not the player body]\n"
                "                    [--out DIR] [--frames LO-HI] [--size WxH]\n"
                "                    [--yaw N] [--scale N] [--by-label] [--sheet]\n");
        return 2;
    }

    if( !load_side_dat2(&ra, a_rev, a_cache, a_seq, a_model) )
        return 1;
    if( b_dat2 )
    {
        int b_model_id = b_model_arg ? atoi(b_model_arg) : -1;
        if( !load_side_dat2(&rb, b_rev, b_cache, b_seq, b_model_id) ) return 1;
    }
    else if( !load_side_dat1(&rb, b_models, b_model_arg, b_anim) ) return 1;

    printf("A %-28s verts=%6d faces=%6d transforms=%3d frames=%d\n",
           ra.name, ra.vertex_count, ra.face_count, ra.transform_count, ra.frame_count);
    printf("B %-28s verts=%6d faces=%6d transforms=%3d frames=%d\n",
           rb.name, rb.vertex_count, rb.face_count, rb.transform_count, rb.frame_count);

    if( motion )
    {
        report_motion(&ra, "SOURCE");
        report_motion(&rb, "PORT");
        return 0;
    }
    if( report )
    {
        report_used(&ra, &rb);
        return 0;
    }
    if( hi < 0 )
        hi = (ra.frame_count < rb.frame_count ? ra.frame_count : rb.frame_count) - 1;
    if( !scale )
        scale = auto_scale(&ra, &rb, ph);
    mkdir(out, 0755);

    int n = hi - lo + 1;
    struct Canvas pa = { pw, ph, malloc((size_t)pw * ph * 3) };
    struct Canvas pb = { pw, ph, malloc((size_t)pw * ph * 3) };
    struct Canvas pair = { pw * 2, ph, malloc((size_t)pw * 2 * ph * 3) };
    int cols = n < 8 ? n : 8;
    int rows = (n + cols - 1) / cols;
    struct Canvas sh = { 0, 0, NULL };
    if( sheet )
    {
        sh.w = cols * pw * 2 / 2;
        sh.h = rows * ph / 2;
        sh.px = malloc((size_t)sh.w * sh.h * 3);
        canvas_fill(&sh, 0x101014);
    }

    for( int f = lo; f <= hi; f++ )
    {
        char path[1024];
        canvas_fill(&pa, 0x101014);
        canvas_fill(&pb, 0x141014);
        rig_pose(&ra, f);
        rig_pose(&rb, f);
        if( getenv("ANIM_YRANGE") )
        {
            int amin=1<<30,amax=-(1<<30),bmin=1<<30,bmax=-(1<<30);
            int omin=1<<30,omax=-(1<<30);
            for( int i=0;i<ra.vertex_count;i++){ if(ra.vy[i]<amin)amin=ra.vy[i]; if(ra.vy[i]>amax)amax=ra.vy[i];
                                                 if(ra.oy[i]<omin)omin=ra.oy[i]; if(ra.oy[i]>omax)omax=ra.oy[i]; }
            for( int i=0;i<rb.vertex_count;i++){ if(rb.vy[i]<bmin)bmin=rb.vy[i]; if(rb.vy[i]>bmax)bmax=rb.vy[i]; }
            printf("frame %3d  A y[%6d..%6d]  B y[%6d..%6d]  bind y[%6d..%6d]\n",
                   f, amin, amax, bmin, bmax, omin, omax);
        }
        rig_render(&ra, &pa, yaw, scale, by_label);
        rig_render(&rb, &pb, yaw, scale, by_label);
        canvas_fill(&pair, 0x000000);
        blit(&pair, &pa, 0, 0);
        blit(&pair, &pb, pw, 0);
        snprintf(path, sizeof(path), "%s/frame_%03d.bmp", out, f);
        bmp_write(path, &pair);
        if( sheet )
        {
            /* Half-size thumbnails, source above port, so a whole run reads at
             * a glance and only the frames that diverge need opening. */
            int idx = f - lo;
            int ox = (idx % cols) * pw;
            int oy = (idx / cols) * (ph / 2);
            for( int y = 0; y < ph / 2; y++ )
                for( int x = 0; x < pw; x++ )
                {
                    int sxp = (x * 2) % (pw * 2);
                    const unsigned char* s = &pair.px[((y * 2) * pair.w + sxp) * 3];
                    int dx = ox + x, dy = oy + y;
                    if( dx < sh.w && dy < sh.h )
                        memcpy(&sh.px[(dy * sh.w + dx) * 3], s, 3);
                }
        }
    }
    if( sheet )
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/sheet.bmp", out);
        bmp_write(path, &sh);
    }
    printf("wrote %d frame(s) to %s/\n", n, out);
    return 0;
}
