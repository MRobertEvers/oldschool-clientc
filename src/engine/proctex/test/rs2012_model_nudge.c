/*
 * Apply small bounded geometry/priority moves to an imported OB3 model.
 *
 * Z-fighting on the painter is two surfaces at the same depth with no stable
 * order, and there are exactly two honest cures: make the depths stop being
 * equal (move geometry), or make the winner stop being arbitrary (pin a
 * priority). This tool is the APPLY half of both: it executes a move list and
 * re-encodes the files, byte-verified. It proposes nothing and judges nothing
 * — the audit driver (tools/rs2012_backport_audit) proposes moves off measured
 * tie pixels and judges every candidate end-to-end through the real renderer,
 * keeping the proposer and the judge different code on purpose.
 *
 * Multiple --in parts are measured TOGETHER (a merged npc shares label space
 * across its parts); each --out receives the slice belonging to the --in at
 * the same position. Rig-group moves address the merged label space; face
 * lists are PART-LOCAL indices, scoped by an explicit part number.
 *
 * Moves file, one per line, applied in order. Amounts are analysis units (the
 * viewer's space; version-13+ raw coordinates are 4x and the shift is handled
 * here):
 *   inflate <group> <amount>            each vertex of the rig group moves
 *                                       away from (+) or toward (-) the
 *                                       group's merged centroid
 *   translate <group> <dx> <dy> <dz>
 *   patch-normal <part> <amount> <f0,f1,...>
 *                                       move the listed faces' vertices along
 *                                       the patch's area-weighted mean normal
 *                                       — the right direction for separating
 *                                       stacked shells
 *   verts-normal <part> <amount> <v0,v1,...>
 *                                       move each listed vertex along its OWN
 *                                       surface normal (area-weighted over
 *                                       every face touching it) — the finest
 *                                       tier, one vertex at a time
 *   quantize <group|-1> <step>          snap the group's (-1: every group's)
 *                                       vertex coordinates to multiples of
 *                                       <step> analysis units. Runs LAST,
 *                                       after all other geometry moves — the
 *                                       coarse-grid look of hand-built OSRS
 *                                       meshes is a final projection
 *   strip-priorities                    drop every priority byte
 *   bulk-flex                           every face to band 10 (flexible): the
 *                                       depth-sorted bulk a pinned patch can
 *                                       splice into at its own depth
 *   priority-faces <part> <band> <f0,f1,...>
 *                                       pin the listed faces into a band, so
 *                                       an exact-depth tie resolves the same
 *                                       way every frame
 *
 * Build and run:
 *   make -C src rs2012-model-nudge
 *   src/build_win64_opt/rs2012_model_nudge \
 *       --in a.ob3 --in b.ob3 --out a2.ob3 --out b2.ob3 --moves moves.txt
 */

#include "datatypes/model.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODELS 16
#define MAX_MOVES 512
#define MAX_GROUPS 256

enum move_kind
{
    MV_INFLATE,
    MV_TRANSLATE,
    MV_PATCH_NORMAL,
    MV_VERTS_NORMAL,
    MV_QUANTIZE,
    MV_STRIP_PRIORITIES,
    MV_BULK_FLEX,
    MV_PRIORITY_FACES,
};

struct move
{
    enum move_kind kind;
    int group;
    double amount;
    int dx, dy, dz;
    int part;
    int band;
    int* faces;
    int face_count;
};

static uint8_t*
read_file(const char* path, long* out_size)
{
    FILE* f = fopen(path, "rb");
    uint8_t* bytes;
    long size;

    if( !f )
        return NULL;
    if( fseek(f, 0, SEEK_END) != 0 )
    {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if( size <= 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return NULL;
    }
    bytes = (uint8_t*)malloc((size_t)size);
    if( !bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size )
    {
        free(bytes);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = size;
    return bytes;
}

static bool
write_file(const char* path, const uint8_t* bytes, uint32_t size)
{
    FILE* f = fopen(path, "wb");
    bool ok;

    if( !f )
        return false;
    ok = fwrite(bytes, 1, size, f) == size;
    fclose(f);
    return ok;
}

static int*
parse_face_list(const char* s, int* out_count)
{
    int capacity = 1;
    int* faces;
    int count = 0;

    for( const char* c = s; *c; c++ )
        if( *c == ',' )
            capacity++;
    faces = (int*)malloc((size_t)capacity * sizeof(int));
    if( !faces )
        return NULL;
    while( *s && count < capacity )
    {
        faces[count++] = (int)strtol(s, (char**)&s, 10);
        if( *s == ',' )
            s++;
        else
            break;
    }
    *out_count = count;
    return faces;
}

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage: %s --in FILE.ob3 [--in FILE.ob3 ...]\n"
        "       --out FILE.ob3 [--out FILE.ob3 ...] --moves FILE\n",
        argv0);
}

int
main(int argc, char** argv)
{
    const char* in_paths[MAX_MODELS];
    const char* out_paths[MAX_MODELS];
    int in_count = 0, out_count = 0;
    const char* moves_path = NULL;
    struct move moves[MAX_MOVES];
    int move_count = 0;

    struct RSCache_Model* models[MAX_MODELS] = { 0 };
    struct RSCache_ModelProvenance* provs[MAX_MODELS] = { 0 };
    uint8_t* file_bytes[MAX_MODELS] = { 0 };

    double centroid_x[MAX_GROUPS] = { 0 }, centroid_y[MAX_GROUPS] = { 0 },
           centroid_z[MAX_GROUPS] = { 0 };
    long group_verts[MAX_GROUPS] = { 0 };
    long moved_total = 0;
    int rc = 1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--in") == 0 && i + 1 < argc && in_count < MAX_MODELS )
            in_paths[in_count++] = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc && out_count < MAX_MODELS )
            out_paths[out_count++] = argv[++i];
        else if( strcmp(argv[i], "--moves") == 0 && i + 1 < argc )
            moves_path = argv[++i];
        else
        {
            usage(argv[0]);
            return 2;
        }
    }
    if( in_count == 0 || in_count != out_count || !moves_path )
    {
        usage(argv[0]);
        return 2;
    }

    /* ---- moves (a patch list can run to thousands of characters) ---- */
    {
        static char line[1 << 16];
        FILE* f = fopen(moves_path, "r");
        char list_buf[1 << 16];

        if( !f )
        {
            fprintf(stderr, "rs2012_model_nudge: cannot read %s\n", moves_path);
            return 1;
        }
        while( fgets(line, sizeof(line), f) && move_count < MAX_MOVES )
        {
            struct move* mv = &moves[move_count];
            memset(mv, 0, sizeof(*mv));
            if( sscanf(line, "inflate %d %lf", &mv->group, &mv->amount) == 2 )
            {
                mv->kind = MV_INFLATE;
                move_count++;
            }
            else if( sscanf(line, "translate %d %d %d %d", &mv->group, &mv->dx, &mv->dy,
                            &mv->dz) == 4 )
            {
                mv->kind = MV_TRANSLATE;
                move_count++;
            }
            else if( sscanf(line, "quantize %d %lf", &mv->group, &mv->amount) == 2 )
            {
                mv->kind = MV_QUANTIZE;
                move_count++;
            }
            else if( sscanf(line, "patch-normal %d %lf %65535s", &mv->part, &mv->amount,
                            list_buf) == 3 )
            {
                mv->kind = MV_PATCH_NORMAL;
                mv->faces = parse_face_list(list_buf, &mv->face_count);
                if( mv->faces && mv->face_count > 0 )
                    move_count++;
            }
            else if( sscanf(line, "verts-normal %d %lf %65535s", &mv->part, &mv->amount,
                            list_buf) == 3 )
            {
                mv->kind = MV_VERTS_NORMAL;
                mv->faces = parse_face_list(list_buf, &mv->face_count);
                if( mv->faces && mv->face_count > 0 )
                    move_count++;
            }
            else if( sscanf(line, "priority-faces %d %d %65535s", &mv->part, &mv->band,
                            list_buf) == 3 )
            {
                mv->kind = MV_PRIORITY_FACES;
                mv->faces = parse_face_list(list_buf, &mv->face_count);
                if( mv->faces && mv->face_count > 0 )
                    move_count++;
            }
            else if( strncmp(line, "strip-priorities", 16) == 0 )
            {
                mv->kind = MV_STRIP_PRIORITIES;
                move_count++;
            }
            else if( strncmp(line, "bulk-flex", 9) == 0 )
            {
                mv->kind = MV_BULK_FLEX;
                move_count++;
            }
            else if( line[0] != '\n' && line[0] != '#' && line[0] != '\r' &&
                     line[0] != '\0' )
                fprintf(stderr, "rs2012_model_nudge: ignoring line: %.60s\n", line);
        }
        fclose(f);
    }
    if( move_count == 0 )
    {
        fprintf(stderr, "rs2012_model_nudge: no moves in %s\n", moves_path);
        return 1;
    }

    /* ---- decode all parts ---- */
    for( int i = 0; i < in_count; i++ )
    {
        long size = 0;
        file_bytes[i] = read_file(in_paths[i], &size);
        if( !file_bytes[i] )
        {
            fprintf(stderr, "rs2012_model_nudge: cannot read %s\n", in_paths[i]);
            goto done;
        }
        models[i] = RSCache_ModelNewDecodeProvenance(file_bytes[i], (int)size, &provs[i]);
        if( !models[i] || !provs[i] )
        {
            fprintf(stderr, "rs2012_model_nudge: cannot decode %s\n", in_paths[i]);
            goto done;
        }
        if( !models[i]->vertex_bone_map )
        {
            fprintf(stderr, "rs2012_model_nudge: %s carries no vertex groups\n", in_paths[i]);
            goto done;
        }
    }

    /* ---- merged centroids, in analysis units (raw >> shift) ---- */
    for( int i = 0; i < in_count; i++ )
    {
        struct RSCache_Model* m = models[i];
        int const shift = m->format_version >= 13 ? 2 : 0;
        for( int v = 0; v < m->vertex_count; v++ )
        {
            int const g = m->vertex_bone_map[v];
            centroid_x[g] += (double)(m->vertices_x[v] >> shift);
            centroid_y[g] += (double)(m->vertices_y[v] >> shift);
            centroid_z[g] += (double)(m->vertices_z[v] >> shift);
            group_verts[g]++;
        }
    }
    for( int g = 0; g < MAX_GROUPS; g++ )
        if( group_verts[g] )
        {
            centroid_x[g] /= (double)group_verts[g];
            centroid_y[g] /= (double)group_verts[g];
            centroid_z[g] /= (double)group_verts[g];
        }
    for( int k = 0; k < move_count; k++ )
    {
        struct move const* mv = &moves[k];
        if( (mv->kind == MV_INFLATE || mv->kind == MV_TRANSLATE) &&
            (mv->group < 0 || mv->group >= MAX_GROUPS || group_verts[mv->group] == 0) )
        {
            fprintf(stderr, "rs2012_model_nudge: group %d has no vertices\n", mv->group);
            goto done;
        }
        if( (mv->kind == MV_PATCH_NORMAL || mv->kind == MV_VERTS_NORMAL ||
             mv->kind == MV_PRIORITY_FACES) &&
            (mv->part < 0 || mv->part >= in_count) )
        {
            fprintf(stderr, "rs2012_model_nudge: no part %d\n", mv->part);
            goto done;
        }
        if( mv->kind == MV_QUANTIZE &&
            (mv->amount <= 0.0 ||
             (mv->group != -1 && (mv->group < 0 || mv->group >= MAX_GROUPS ||
                                  group_verts[mv->group] == 0))) )
        {
            fprintf(stderr, "rs2012_model_nudge: bad quantize (group %d step %g)\n",
                    mv->group, mv->amount);
            goto done;
        }
    }

    /* ---- apply, encode, verify, write ---- */
    for( int i = 0; i < in_count; i++ )
    {
        struct RSCache_Model* m = models[i];
        struct RSCache_ModelProvenance* p = provs[i];
        int const shift = m->format_version >= 13 ? 2 : 0;
        uint8_t* encoded = NULL;
        uint32_t bound, written;
        long moved = 0;

        /* group moves first: they accumulate per vertex */
        for( int v = 0; v < m->vertex_count; v++ )
        {
            int const g = m->vertex_bone_map[v];
            int dx = 0, dy = 0, dz = 0;

            for( int k = 0; k < move_count; k++ )
            {
                struct move const* mv = &moves[k];
                if( (mv->kind != MV_INFLATE && mv->kind != MV_TRANSLATE) ||
                    mv->group != g )
                    continue;
                if( mv->kind == MV_INFLATE )
                {
                    double const rx = (double)(m->vertices_x[v] >> shift) - centroid_x[g];
                    double const ry = (double)(m->vertices_y[v] >> shift) - centroid_y[g];
                    double const rz = (double)(m->vertices_z[v] >> shift) - centroid_z[g];
                    double const len = sqrt(rx * rx + ry * ry + rz * rz);
                    /* A vertex on the centroid has no outward; its neighbours
                     * carry the separation. */
                    if( len < 0.5 )
                        continue;
                    dx += (int)lround(rx / len * mv->amount);
                    dy += (int)lround(ry / len * mv->amount);
                    dz += (int)lround(rz / len * mv->amount);
                }
                else
                {
                    dx += mv->dx;
                    dy += mv->dy;
                    dz += mv->dz;
                }
            }
            if( dx || dy || dz )
            {
                m->vertices_x[v] += dx << shift;
                m->vertices_y[v] += dy << shift;
                m->vertices_z[v] += dz << shift;
                moved++;
            }
        }

        /* patch-normal moves: the listed faces' area-weighted mean normal,
         * then every vertex those faces touch slides along it */
        for( int k = 0; k < move_count; k++ )
        {
            struct move const* mv = &moves[k];
            double nx = 0, ny = 0, nz = 0, len;
            uint8_t* touched;
            int ddx, ddy, ddz;

            if( mv->kind != MV_PATCH_NORMAL || mv->part != i )
                continue;
            touched = (uint8_t*)calloc((size_t)m->vertex_count, 1);
            if( !touched )
                goto done;
            for( int j = 0; j < mv->face_count; j++ )
            {
                int const face = mv->faces[j];
                int ia, ib, ic;
                double ax, ay, az, bx, by, bz;

                if( face < 0 || face >= m->face_count )
                    continue;
                ia = m->face_indices_a[face];
                ib = m->face_indices_b[face];
                ic = m->face_indices_c[face];
                touched[ia] = touched[ib] = touched[ic] = 1;
                ax = (double)(m->vertices_x[ib] - m->vertices_x[ia]);
                ay = (double)(m->vertices_y[ib] - m->vertices_y[ia]);
                az = (double)(m->vertices_z[ib] - m->vertices_z[ia]);
                bx = (double)(m->vertices_x[ic] - m->vertices_x[ia]);
                by = (double)(m->vertices_y[ic] - m->vertices_y[ia]);
                bz = (double)(m->vertices_z[ic] - m->vertices_z[ia]);
                nx += ay * bz - az * by;
                ny += az * bx - ax * bz;
                nz += ax * by - ay * bx;
            }
            len = sqrt(nx * nx + ny * ny + nz * nz);
            if( len < 1e-6 )
            {
                fprintf(stderr,
                        "rs2012_model_nudge: patch normal degenerate (part %d, %d faces)\n",
                        i, mv->face_count);
                free(touched);
                continue;
            }
            ddx = (int)lround(nx / len * mv->amount);
            ddy = (int)lround(ny / len * mv->amount);
            ddz = (int)lround(nz / len * mv->amount);
            if( ddx || ddy || ddz )
                for( int v = 0; v < m->vertex_count; v++ )
                    if( touched[v] )
                    {
                        m->vertices_x[v] += ddx << shift;
                        m->vertices_y[v] += ddy << shift;
                        m->vertices_z[v] += ddz << shift;
                        moved++;
                    }
            free(touched);
        }

        /* verts-normal moves: each listed vertex slides along its own
         * area-weighted surface normal (all faces touching it, listed or not,
         * so the direction is the true local surface) */
        for( int k = 0; k < move_count; k++ )
        {
            struct move const* mv = &moves[k];

            if( mv->kind != MV_VERTS_NORMAL || mv->part != i )
                continue;
            for( int j = 0; j < mv->face_count; j++ )
            {
                int const vtx = mv->faces[j];
                double nx = 0, ny = 0, nz = 0, len;

                if( vtx < 0 || vtx >= m->vertex_count )
                    continue;
                for( int face = 0; face < m->face_count; face++ )
                {
                    int const ia = m->face_indices_a[face];
                    int const ib = m->face_indices_b[face];
                    int const ic = m->face_indices_c[face];
                    double ax, ay, az, bx, by, bz;

                    if( ia != vtx && ib != vtx && ic != vtx )
                        continue;
                    ax = (double)(m->vertices_x[ib] - m->vertices_x[ia]);
                    ay = (double)(m->vertices_y[ib] - m->vertices_y[ia]);
                    az = (double)(m->vertices_z[ib] - m->vertices_z[ia]);
                    bx = (double)(m->vertices_x[ic] - m->vertices_x[ia]);
                    by = (double)(m->vertices_y[ic] - m->vertices_y[ia]);
                    bz = (double)(m->vertices_z[ic] - m->vertices_z[ia]);
                    nx += ay * bz - az * by;
                    ny += az * bx - ax * bz;
                    nz += ax * by - ay * bx;
                }
                len = sqrt(nx * nx + ny * ny + nz * nz);
                if( len < 1e-6 )
                    continue;
                {
                    int const ddx = (int)lround(nx / len * mv->amount);
                    int const ddy = (int)lround(ny / len * mv->amount);
                    int const ddz = (int)lround(nz / len * mv->amount);
                    if( ddx || ddy || ddz )
                    {
                        m->vertices_x[vtx] += ddx << shift;
                        m->vertices_y[vtx] += ddy << shift;
                        m->vertices_z[vtx] += ddz << shift;
                        moved++;
                    }
                }
            }
        }

        /* quantize: final geometry pass, a projection onto a coarse grid in
         * analysis units — for v13+ parts this deliberately discards the 4x
         * sub-unit precision, which is the point */
        for( int k = 0; k < move_count; k++ )
        {
            struct move const* mv = &moves[k];

            if( mv->kind != MV_QUANTIZE )
                continue;
            for( int v = 0; v < m->vertex_count; v++ )
            {
                int const ax = m->vertices_x[v] >> shift;
                int const ay = m->vertices_y[v] >> shift;
                int const az = m->vertices_z[v] >> shift;
                int qx, qy, qz;

                if( mv->group != -1 && m->vertex_bone_map[v] != mv->group )
                    continue;
                qx = (int)lround(lround((double)ax / mv->amount) * mv->amount);
                qy = (int)lround(lround((double)ay / mv->amount) * mv->amount);
                qz = (int)lround(lround((double)az / mv->amount) * mv->amount);
                if( qx != ax || qy != ay || qz != az ||
                    m->vertices_x[v] != (qx << shift) ||
                    m->vertices_y[v] != (qy << shift) ||
                    m->vertices_z[v] != (qz << shift) )
                {
                    m->vertices_x[v] = qx << shift;
                    m->vertices_y[v] = qy << shift;
                    m->vertices_z[v] = qz << shift;
                    moved++;
                }
            }
        }

        /* priority ops, in file order */
        for( int k = 0; k < move_count; k++ )
        {
            struct move const* mv = &moves[k];

            if( mv->kind == MV_STRIP_PRIORITIES )
            {
                free(m->face_priorities);
                m->face_priorities = NULL;
                m->model_priority = 0;
            }
            else if( mv->kind == MV_BULK_FLEX ||
                     (mv->kind == MV_PRIORITY_FACES && mv->part == i &&
                      !m->face_priorities) )
            {
                /* Band 10 is the flexible bulk: it depth-sorts and splices a
                 * hard-banded patch in at its own depth, rather than pinning
                 * everything into layers. The neutral ground priority pins
                 * grow out of. */
                if( !m->face_priorities )
                    m->face_priorities = (uint8_t*)malloc((size_t)m->face_count);
                if( !m->face_priorities )
                    goto done;
                memset(m->face_priorities, 10, (size_t)m->face_count);
                m->model_priority = 255;
            }
            if( mv->kind == MV_PRIORITY_FACES && mv->part == i )
            {
                for( int j = 0; j < mv->face_count; j++ )
                {
                    int const face = mv->faces[j];
                    if( face >= 0 && face < m->face_count )
                        m->face_priorities[face] = (uint8_t)mv->band;
                }
                m->model_priority = 255;
            }
        }

        /* The encoder prefers the provenance's recorded header; its priority
         * byte must agree with the array or the array is silently dropped. */
        if( p->header_flag_count > 1 )
            p->header_flags[1] = m->face_priorities ? 255 : 0;

        bound = RSCache_ModelEncodeBound(m, p);
        encoded = bound ? (uint8_t*)malloc(bound) : NULL;
        written = encoded ? RSCache_ModelEncodeFormat(m, p, p->format, encoded, bound) : 0;
        if( !written )
        {
            fprintf(stderr, "rs2012_model_nudge: cannot encode %s\n", out_paths[i]);
            free(encoded);
            goto done;
        }

        /* Prove the file that lands decodes back to what was intended. */
        {
            struct RSCache_ModelProvenance* cp = NULL;
            struct RSCache_Model* check =
                RSCache_ModelNewDecodeProvenance(encoded, (int)written, &cp);
            bool ok = check && cp && check->vertex_count == m->vertex_count &&
                      check->face_count == m->face_count &&
                      memcmp(check->vertices_x, m->vertices_x,
                             (size_t)m->vertex_count * sizeof(int)) == 0 &&
                      memcmp(check->vertices_y, m->vertices_y,
                             (size_t)m->vertex_count * sizeof(int)) == 0 &&
                      memcmp(check->vertices_z, m->vertices_z,
                             (size_t)m->vertex_count * sizeof(int)) == 0 &&
                      (!m->face_priorities) == (!check->face_priorities) &&
                      (!m->face_priorities ||
                       memcmp(check->face_priorities, m->face_priorities,
                              (size_t)m->face_count) == 0);
            RSCache_ModelFree(check);
            RSCache_ModelProvenanceFree(cp);
            if( !ok )
            {
                fprintf(stderr, "rs2012_model_nudge: %s failed its decode check\n",
                        out_paths[i]);
                free(encoded);
                goto done;
            }
        }

        if( !write_file(out_paths[i], encoded, written) )
        {
            fprintf(stderr, "rs2012_model_nudge: cannot write %s\n", out_paths[i]);
            free(encoded);
            goto done;
        }
        free(encoded);
        moved_total += moved;
        printf(
            "rs2012_model_nudge: %s -> %s (%ld vertex moves, priorities %s, %u bytes)\n",
            in_paths[i],
            out_paths[i],
            moved,
            m->face_priorities ? "per-face" : "none",
            written);
    }

    printf("rs2012_model_nudge: %d moves, %ld vertex moves\n", move_count, moved_total);
    rc = 0;

done:
    for( int i = 0; i < in_count; i++ )
    {
        RSCache_ModelFree(models[i]);
        RSCache_ModelProvenanceFree(provs[i]);
        free(file_bytes[i]);
    }
    for( int k = 0; k < move_count; k++ )
        free(moves[k].faces);
    return rc;
}
