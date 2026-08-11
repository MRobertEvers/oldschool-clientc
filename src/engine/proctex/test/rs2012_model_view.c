/*
 * Standalone OB3 model viewer — renders a model through the real ToriDraw
 * painter's-algorithm sorter and writes a multi-angle contact sheet BMP.
 *
 * This exists to make face-priority authoring on the imported RS2012 lane an
 * iteration loop instead of a full client boot. It links only rscache,
 * toridraw and the two model adaptors: no cache, no window, no game.
 *
 * The point is the SORT, so it deliberately renders the same way the client
 * does — ToriDraw_RenderModel1Project / 2SortFaces / 3Raster — rather than
 * with any private ordering. Whatever priorities the .ob3 carries are honoured
 * exactly as the client honours them.
 *
 * Build and run:
 *   make -C src rs2012-model-view
 *   src/build/rs2012_model_view --model <a.ob3> [--model <b.ob3>] --out shot.bmp
 *
 * Multiple --model arguments are merged the way an npc's model1/model2 are,
 * through ToriDraw_ModelNewMerge, so the merged priority behaviour is the one
 * under test.
 *
 * Options:
 *   --angles N      yaw steps around the model (default 4)
 *   --pitch N       camera pitch, 0..2047 (default 200, slightly above)
 *   --yaw0 N        first yaw (default 0)
 *   --zoom F        framing multiplier, >1 is closer (default 1.0)
 *   --tile N        tile pixel size (default 320)
 *   --textures      keep face texture ids (default: stripped, no texture map)
 *   --ignore-priorities   render as if the model carried none
 *   --stats         print the face-priority histogram and model shape
 *   --zbuffer       draw through the depth-tested kernels (the model opts in
 *                   with TORIDRAW_MODEL_FLAG_ZBUFFER and the scene carries the
 *                   z-buffer), so the model resolves itself per pixel
 *   --compare       render BOTH ways and report what the depth test changed;
 *                   writes the painter sheet to --out and the depth-tested one
 *                   to --out-zbuffer, with --diff-out marking the pixels that
 *                   differ. Implies --score, because the number worth knowing
 *                   is how much of the change lands on pixels the sort had
 *                   wrong.
 *   --out-zbuffer F where --compare writes the depth-tested sheet
 *   --diff-out F    where --compare writes the difference mask
 *   --order-check   draw the model depth-tested twice, the second time with the
 *                   face order reversed, and report the pixels that differ.
 *                   A depth buffer that works makes OPAQUE output independent
 *                   of the order faces arrive in, so this grades the kernels
 *                   without needing a reference picture to compare against.
 *                   Translucent faces are order-dependent by definition and are
 *                   the expected residual.
 */

#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/model.h"
#include "filelist.h"

#include "engine/toridraw_animation_from_rscache.h"

#include "engine/torirs_model_from_rscache.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_types.h"

#include <bmp.h>
#include <toridraw.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODELS 16
#define MAX_ANGLES 16
#define MAX_PITCHES 8

/* ToriDraw_ModelDropNonSdTextures is the only thing in the adaptor that reaches
 * a CacheProvider, and this tool never calls it. Satisfying the link here is
 * cheaper than dragging the provider (and its cache) into a file viewer. */
bool
CacheProvider_TextureIsSd(struct CacheProvider* provider, int texture_id);
bool
CacheProvider_TextureIsSd(struct CacheProvider* provider, int texture_id)
{
    (void)provider;
    (void)texture_id;
    return true;
}

/**
 * Load the sequence's frames the way the client loads them: out of a packed
 * cache, through the profile.
 *
 * Reading the lane's staged `.anim` and `.base` files directly does not work,
 * and the reason is worth recording. Those are pass-through assets — the
 * importer copies the bytes — so what is on disk is encoded in whatever
 * revision the SOURCE used, while the codec that decodes them is a property of
 * the DESTINATION cache. For this lane the two disagree: the frames decode as
 * osrs239's frame V1 (V2 fails outright, so they were re-encoded), but the
 * framemap does not decode as osrs239's framemap V1 — decoded that way its
 * transform types come out wrong, every op reads as a scale, and the first
 * frame collapses all 6,223 vertices onto a single point.
 *
 * Going through the cache removes the guess entirely: `RSCache` carries the
 * revision, the profile picks the codec per type, and the poses measured here
 * are by construction the poses the client draws. It also means the tool
 * measures what shipped rather than what is staged.
 *
 * `frame_ids` are the sequence config's own packed values: group << 16 | file.
 */
static struct ToriDraw_Animation*
load_animation_from_cache(
    const char* cache_dir,
    const int* frame_ids,
    int frame_count)
{
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    struct RSCache_Dat2Framemap* framemap = NULL;
    struct RSCache_Dat2Frame** frames = NULL;
    int* delays = NULL;
    struct ToriDraw_Animation* out = NULL;

    if( !disk )
    {
        fprintf(stderr, "rs2012_face_priorities: cannot open cache %s\n", cache_dir);
        return NULL;
    }
    /* The profile is the whole point of going through the cache: it is what
     * resolves a type to a codec, and without it the loaders fall back to
     * defaults that do not match this cache. */
    {
        struct RSCache profile =
            RSCache_ProfileForIdentity(RSCACHE_GAME_OLDSCHOOL, RSCACHE_EPOCH_DAT2, 239, 0u);
        RSCache_Dat2DiskSetProfile(disk, &profile);
    }
    frames = (struct RSCache_Dat2Frame**)calloc((size_t)frame_count, sizeof(*frames));
    delays = (int*)malloc((size_t)frame_count * sizeof(int));
    if( !frames || !delays )
        goto done;

    for( int i = 0; i < frame_count; i++ )
    {
        int const group = frame_ids[i] >> 16;
        int const file = frame_ids[i] & 0xFFFF;
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(disk, RSCACHE_DAT2_TABLE_ANIMATIONS, group);
        struct RSCache_FileList* files = NULL;
        int pos = -1;

        if( !archive )
        {
            fprintf(stderr, "rs2012_face_priorities: no animation group %d\n", group);
            goto done;
        }
        RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
        files =
            RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        for( int k = 0; files && k < archive->file_count; k++ )
            if( archive->file_ids[k] == file )
                pos = k;

        if( files && pos >= 0 && pos < files->file_count )
        {
            /* All frames of a sequence share one framemap; load it off the
             * first and reuse, exactly as task_dat2_sequence_load does. */
            if( !framemap )
            {
                int const id = RSCache_Dat2FramemapIdFromFrameArchive(
                    files->files[pos], files->file_sizes[pos]);
                /* Through the cache and its profile, which is exactly what the
                 * client does. Neither this nor an explicit V3 read produces a
                 * usable pose on this lane — see docs/rs2012_qbd_wake/README.md
                 * — but this is the path whose output is the client's, so it is
                 * the one worth rendering. */
                if( id >= 0 )
                    framemap = RSCache_Dat2FramemapNewFromCache(disk, id);
            }
            /* Decoded from the archive MEMBER, not via
             * RSCache_Dat2FrameNewFromCache: that helper treats the packed id
             * as a group of its own, which is the OSRS layout. This lane packs
             * a sequence's frames as members of one group, so the member is
             * what has to be handed to the decoder. task_dat2_sequence_load
             * does the same. */
            if( framemap )
                frames[i] = RSCache_Dat2FrameNewDecodeProfile(
                    RSCache_Dat2DiskProfile(disk),
                    frame_ids[i],
                    framemap,
                    files->files[pos],
                    files->file_sizes[pos]);
        }
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);

        if( !frames[i] )
        {
            fprintf(
                stderr, "rs2012_face_priorities: cannot load frame %d\n", frame_ids[i]);
            goto done;
        }
        delays[i] = 1;
    }

    out = ToriDraw_AnimationFromRSCache(
        framemap, (struct RSCache_Dat2Frame const* const*)frames, delays, frame_count, 0);

done:
    if( frames )
        for( int i = 0; i < frame_count; i++ )
            RSCache_Dat2FrameFree(frames[i]);
    free(frames);
    free(delays);
    RSCache_Dat2FramemapFree(framemap);
    RSCache_Dat2DiskFree(disk);
    return out;
}

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

static struct ToriDraw_Model*
load_ob3(const char* path)
{
    long size = 0;
    uint8_t* bytes = read_file(path, &size);
    struct RSCache_Model* rs;
    struct ToriRS_Model* mid;
    struct ToriDraw_Model* out;

    if( !bytes )
    {
        fprintf(stderr, "rs2012_model_view: cannot read %s\n", path);
        return NULL;
    }

    rs = RSCache_ModelNewDecode(bytes, (int)size);
    free(bytes);
    if( !rs )
    {
        fprintf(stderr, "rs2012_model_view: cannot decode %s\n", path);
        return NULL;
    }
    /* Moves the arrays out of rs, leaving it hollow but still freeable. */
    mid = ToriRS_ModelFromRSCache(rs);
    RSCache_ModelFree(rs);
    if( !mid )
        return NULL;
    out = ToriDraw_ModelFromToriRS(mid);
    ToriRS_ModelFree(mid);
    return out;
}

static int
face_priority_of(const struct ToriDraw_Model* m, int face)
{
    int byte;
    if( !m->face_priorities )
        return m->model_priority;
    byte = m->face_priorities[face >> 1];
    return (face & 1) ? (byte >> 4) : (byte & 0x0F);
}

static void
print_stats(const struct ToriDraw_Model* m)
{
    int hist[16] = { 0 };
    int min_y = 1 << 30, max_y = -(1 << 30);
    int textured = 0, alpha = 0;

    for( int f = 0; f < m->face_count; f++ )
    {
        hist[face_priority_of(m, f) & 15]++;
        if( m->face_textures && m->face_textures[f] >= 0 )
            textured++;
        if( m->face_alphas && m->face_alphas[f] )
            alpha++;
    }
    for( int v = 0; v < m->vertex_count; v++ )
    {
        if( m->vertices_y[v] < min_y )
            min_y = m->vertices_y[v];
        if( m->vertices_y[v] > max_y )
            max_y = m->vertices_y[v];
    }

    printf(
        "model: %d vertices, %d faces, y %d..%d, textured %d, alpha %d, "
        "per-face priorities %s (model_priority %u)\n",
        m->vertex_count,
        m->face_count,
        min_y,
        max_y,
        textured,
        alpha,
        m->face_priorities ? "yes" : "no",
        (unsigned)m->model_priority);
    printf("  priority histogram:");
    for( int p = 0; p < 13; p++ )
        if( hist[p] )
            printf(" %d=%d", p, hist[p]);
    printf("\n");
}

static void
strip_textures(struct ToriDraw_Model* m)
{
    if( !m->face_textures )
        return;
    for( int f = 0; f < m->face_count; f++ )
        m->face_textures[f] = (faceint_t)-1;
    if( m->face_texture_coords )
        for( int f = 0; f < m->face_count; f++ )
            m->face_texture_coords[f] = (faceint_t)-1;
}

/* --- lane textures -------------------------------------------------------
 *
 * The viewer draws through the real raster, and the raster reads the scene's
 * texture map — so without one every textured face is skipped and a textured
 * model renders as holes. The client fills that map from the packed cache
 * through the provider and the task queue, which is far too much machinery to
 * drag in here.
 *
 * These read the bake's own output instead: the sprite BMPs and the texture
 * records in the content tree, which is exactly what the cache packer consumes.
 * One caveat worth knowing when comparing a viewer frame against the client:
 * the client re-quantises through the pack palette and applies gamma 0.8, so
 * its texels are slightly darker. Both sides of an A/B here see the same
 * texels, so a difference between them is the kernel and not the loader.
 */

/** 32bpp bottom-up BMP, the only shape rs2012_material_bake writes. */
static int*
view_bmp_read(const char* path, int* out_w, int* out_h)
{
    FILE* file = fopen(path, "rb");
    unsigned char header[54];
    int* pixels = NULL;
    long offset, w, h;

    if( !file )
        return NULL;
    if( fread(header, 1, sizeof(header), file) != sizeof(header) || header[0] != 'B' ||
        header[1] != 'M' || header[28] != 32 )
    {
        fclose(file);
        return NULL;
    }
    offset = (long)header[10] | ((long)header[11] << 8) | ((long)header[12] << 16) |
             ((long)header[13] << 24);
    w = (long)header[18] | ((long)header[19] << 8) | ((long)header[20] << 16) |
        ((long)header[21] << 24);
    h = (long)header[22] | ((long)header[23] << 8) | ((long)header[24] << 16) |
        ((long)header[25] << 24);
    if( w <= 0 || h <= 0 || w > 4096 || h > 4096 )
    {
        fclose(file);
        return NULL;
    }
    pixels = (int*)malloc((size_t)w * (size_t)h * sizeof(int));
    if( !pixels || fseek(file, offset, SEEK_SET) != 0 )
    {
        free(pixels);
        fclose(file);
        return NULL;
    }
    for( long y = h - 1; y >= 0; y-- )
    {
        for( long x = 0; x < w; x++ )
        {
            unsigned char bgra[4];
            if( fread(bgra, 1, 4, file) != 4 )
            {
                free(pixels);
                fclose(file);
                return NULL;
            }
            pixels[y * w + x] = (int)(((uint32_t)bgra[3] << 24) | ((uint32_t)bgra[2] << 16) |
                                      ((uint32_t)bgra[1] << 8) | (uint32_t)bgra[0]);
        }
    }
    fclose(file);
    *out_w = (int)w;
    *out_h = (int)h;
    return pixels;
}

/**
 * Value of `key=` in one texture record, or NULL.
 *
 * `block` points just past the `[name]` header; the search stops at the next
 * one. Without that bound every lookup succeeds by finding the key in a later
 * record, and every texture reads as carrying every flag.
 */
static const char*
view_record_field(const char* block, const char* key)
{
    static char value[128];
    const char* p = block;
    const char* end = strstr(block, "\n[");
    size_t klen = strlen(key);

    while( p && *p && (!end || p < end) )
    {
        const char* eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        if( len > klen && strncmp(p, key, klen) == 0 && p[klen] == '=' )
        {
            size_t vlen = len - klen - 1;
            if( vlen >= sizeof(value) )
                vlen = sizeof(value) - 1;
            memcpy(value, p + klen + 1, vlen);
            value[vlen] = '\0';
            while( vlen > 0 && (value[vlen - 1] == '\r' || value[vlen - 1] == ' ') )
                value[--vlen] = '\0';
            return value;
        }
        p = eol ? eol + 1 : NULL;
    }
    return NULL;
}

static char*
view_read_file(const char* path)
{
    FILE* file = fopen(path, "rb");
    long size;
    char* text;

    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(file);
        return NULL;
    }
    text = (char*)malloc((size_t)size + 1);
    if( !text )
    {
        fclose(file);
        return NULL;
    }
    if( fread(text, 1, (size_t)size, file) != (size_t)size )
    {
        free(text);
        fclose(file);
        return NULL;
    }
    text[size] = '\0';
    fclose(file);
    return text;
}

/**
 * Publish every lane texture into the scene map, from `<tree>`.
 */
static int
view_load_lane_textures(struct ToriDraw_Scene* scene, const char* tree, bool verbose)
{
    char path[2048];
    char* records;
    char* index;
    int loaded = 0;

    snprintf(path, sizeof(path), "%s/ported/rs2012_qbd_td/textures/texture_0.texture", tree);
    records = view_read_file(path);
    snprintf(path, sizeof(path), "%s/ported/rs2012_qbd_td/textures/texture_0.compack", tree);
    index = view_read_file(path);
    if( !records || !index )
    {
        fprintf(stderr, "rs2012_model_view: no lane textures under %s\n", tree);
        free(records);
        free(index);
        return 0;
    }

    for( char* line = strtok(index, "\r\n"); line; line = strtok(NULL, "\r\n") )
    {
        char name[128], block_head[160];
        int id;
        char* block;
        const char* sprite_field;
        int width = 0, height = 0;
        int* texels;
        struct ToriDraw_Texture* texture;

        if( sscanf(line, "%d=%127s", &id, name) != 2 )
            continue;

        snprintf(block_head, sizeof(block_head), "[%s]\n", name);
        block = strstr(records, block_head);
        if( !block )
            continue;
        block += strlen(block_head);

        sprite_field = view_record_field(block, "sprite1");
        if( !sprite_field )
            continue;

        snprintf(path, sizeof(path), "%s/sprites/ported/rs2012_qbd_td/%s/0.bmp", tree, name);
        texels = view_bmp_read(path, &width, &height);
        if( !texels )
            continue;

        texture = (struct ToriDraw_Texture*)calloc(1, sizeof(*texture));
        if( !texture )
        {
            free(texels);
            continue;
        }
        texture->texels = texels;
        texture->width = width;
        texture->height = height;
        texture->opaque = view_record_field(block, "opaque") != NULL;
        ToriDraw_SceneSetTexture(scene, id, texture);
        loaded++;
    }

    if( verbose )
        fprintf(
            stderr,
            "rs2012_model_view: %d lane textures\n",
            loaded);
    free(records);
    free(index);
    return loaded;
}

/**
 * Move the model so its bounding-box centre sits on the model origin, and
 * rebuild the bounds cylinder around the result.
 *
 * The viewer orbits by rotating the MODEL (position->pitch/yaw) with the
 * camera left at identity, because the camera-pitch form orbits about the eye
 * and swings a model this tall straight out of frame. Rotating the model works
 * only if the pivot is the model's centre, and an npc model's origin is at its
 * feet, so the recentre is what makes the orbit stay framed. Only the viewer
 * does this; nothing is written back to the .ob3.
 */
static void
recenter_model(struct ToriDraw_Model* m, const int* explicit_center)
{
    int min_x = 1 << 30, max_x = -(1 << 30);
    int min_y = 1 << 30, max_y = -(1 << 30);
    int min_z = 1 << 30, max_z = -(1 << 30);
    int cx, cy, cz, radius2 = 0;

    if( m->vertex_count <= 0 )
        return;

    for( int v = 0; v < m->vertex_count; v++ )
    {
        int x = m->vertices_x[v], y = m->vertices_y[v], z = m->vertices_z[v];
        if( x < min_x ) min_x = x;
        if( x > max_x ) max_x = x;
        if( y < min_y ) min_y = y;
        if( y > max_y ) max_y = y;
        if( z < min_z ) min_z = z;
        if( z > max_z ) max_z = z;
    }
    cx = (min_x + max_x) / 2;
    cy = (min_y + max_y) / 2;
    cz = (min_z + max_z) / 2;
    if( explicit_center )
    {
        cx = explicit_center[0];
        cy = explicit_center[1];
        cz = explicit_center[2];
    }

    min_y = 1 << 30;
    max_y = -(1 << 30);
    for( int v = 0; v < m->vertex_count; v++ )
    {
        int x = (m->vertices_x[v] -= (vertexint_t)cx);
        int y = (m->vertices_y[v] -= (vertexint_t)cy);
        int z = (m->vertices_z[v] -= (vertexint_t)cz);
        int r2 = x * x + z * z;
        if( r2 > radius2 )
            radius2 = r2;
        if( y < min_y ) min_y = y;
        if( y > max_y ) max_y = y;
    }

    if( !m->bounds_cylinder )
        m->bounds_cylinder = calloc(1, sizeof(struct ToriDraw_BoundsCylinder));
    if( m->bounds_cylinder )
    {
        struct ToriDraw_BoundsCylinder* bc = m->bounds_cylinder;
        bc->min_y = min_y;
        bc->max_y = max_y;
        bc->radius = (int)sqrt((double)radius2);
        bc->center_to_bottom_edge = (int)sqrt((double)radius2 + (double)min_y * min_y) + 1;
        bc->center_to_top_edge = (int)sqrt((double)radius2 + (double)max_y * max_y) + 1;
        bc->min_z_depth_any_rotation = bc->center_to_top_edge > bc->center_to_bottom_edge
                                           ? bc->center_to_top_edge
                                           : bc->center_to_bottom_edge;
    }
}

static void
drop_priorities(struct ToriDraw_Model* m)
{
    free(m->face_priorities);
    m->face_priorities = NULL;
    m->model_priority = 0;
}

/* ---------------------------------------------------------- sort score -- */

/**
 * How much of the picture the painter's sort gets wrong, in pixels.
 *
 * Eyeballing a contact sheet finds gross breakage and nothing else; a priority
 * edit that fixes the jaw and quietly wrecks a wing looks like an improvement.
 * So score it: rasterize the model twice off the SAME projection, once in the
 * order ToriDraw_RenderModel2SortFaces produced and once with a real per-pixel
 * z-buffer, and count the pixels where the painter's winner is genuinely
 * behind the z-buffer's winner at that pixel.
 *
 * That last clause is what makes the number mean something. Two faces that
 * touch a pixel at equal depth (a shared edge, a coplanar seam) disagree
 * harmlessly and are not counted; only a farther surface painted over a nearer
 * one is a visible error, and it is exactly what a wrong priority band causes.
 *
 * The z-buffer here is the reference the content was authored against, not
 * something the client can run — it exists only to grade the sort.
 */
struct score
{
    long covered;   /* pixels the model touches */
    long wrong;     /* pixels showing a surface behind the true nearest */
    long depth_sum; /* summed depth error over the wrong pixels */
};

static void
raster_id_z(
    const struct ToriDraw_Model* m,
    const struct ToriDraw_Scene* scene,
    const int* order,
    int order_count,
    int w,
    int h,
    int x_center,
    int y_center,
    int* id_painter,
    int* z_painter,
    int* id_zbuf,
    int* z_zbuf)
{
    /* The projection leaves screen coordinates relative to the viewport
     * centre; the raster adds x_center/y_center as it draws. Scoring reads the
     * same scratch, so it has to apply the same offset or the mask lands half
     * a tile away from the picture it is grading. */
    const int* sx = scene->screen_vertices_x;
    const int* sy = scene->screen_vertices_y;
    const int* sz = scene->screen_vertices_z;
    /* screen_vertices_z is camera depth with the model's centre depth removed;
     * adding it back gives the distance to the eye, which is what the
     * reciprocal below has to be taken of. */
    int const mid_z = scene->projected_vertex.z;

    for( int i = 0; i < w * h; i++ )
    {
        id_painter[i] = -1;
        id_zbuf[i] = -1;
        z_painter[i] = 0;
        z_zbuf[i] = 1 << 30;
    }

    for( int o = 0; o < order_count; o++ )
    {
        int const f = order[o];
        int const ia = m->face_indices_a[f];
        int const ib = m->face_indices_b[f];
        int const ic = m->face_indices_c[f];
        int min_x, max_x, min_y, max_y;
        long area;
        int ax, ay, az, bx, by, bz, cx, cy, cz;

        /* A near-clipped vertex carries a sentinel x, not a coordinate. */
        if( sx[ia] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
            sx[ib] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
            sx[ic] == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
            continue;

        ax = sx[ia] + x_center; ay = sy[ia] + y_center; az = sz[ia];
        bx = sx[ib] + x_center; by = sy[ib] + y_center; bz = sz[ib];
        cx = sx[ic] + x_center; cy = sy[ic] + y_center; cz = sz[ic];

        area = (long)(bx - ax) * (cy - ay) - (long)(by - ay) * (cx - ax);
        if( area == 0 )
            continue;

        min_x = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
        max_x = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
        min_y = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
        max_y = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
        if( min_x < 0 ) min_x = 0;
        if( min_y < 0 ) min_y = 0;
        if( max_x >= w ) max_x = w - 1;
        if( max_y >= h ) max_y = h - 1;

        for( int y = min_y; y <= max_y; y++ )
        {
            for( int x = min_x; x <= max_x; x++ )
            {
                long w0 = (long)(bx - ax) * (y - ay) - (long)(by - ay) * (x - ax);
                long w1 = (long)(cx - bx) * (y - by) - (long)(cy - by) * (x - bx);
                long w2 = (long)(ax - cx) * (y - cy) - (long)(ay - cy) * (x - cx);
                double la, lb, lc;
                int z, at;

                if( area > 0 )
                {
                    if( w0 < 0 || w1 < 0 || w2 < 0 )
                        continue;
                }
                else if( w0 > 0 || w1 > 0 || w2 > 0 )
                    continue;

                /* Barycentric from the same edge functions: w1 belongs to a,
                 * w2 to b, w0 to c. */
                la = (double)w1 / (double)area;
                lb = (double)w2 / (double)area;
                lc = (double)w0 / (double)area;

                /*
                 * Perspective-correct: depth is NOT linear in screen space, its
                 * reciprocal is. Interpolating z directly bows the surface away
                 * from where it really is between the corners, and on a model
                 * the size of the QBD — thin, long, and spanning thousands of
                 * units of depth — that error is comparable to the sorting
                 * errors this is supposed to be measuring. A yardstick with the
                 * same magnitude of error as the thing it grades measures
                 * nothing.
                 */
                {
                    double const inv_z = la / (double)(az + mid_z) +
                                         lb / (double)(bz + mid_z) +
                                         lc / (double)(cz + mid_z);

                    if( !(inv_z > 0.0) )
                        continue;
                    z = (int)(1.0 / inv_z) - mid_z;
                }

                at = y * w + x;
                id_painter[at] = f;
                z_painter[at] = z;
                if( z < z_zbuf[at] )
                {
                    z_zbuf[at] = z;
                    id_zbuf[at] = f;
                }
            }
        }
    }
}

/**
 * Paint the z-buffer's answer, flat shaded, so the painter's sort can be seen
 * against the picture the content was authored to produce.
 *
 * The client cannot render this and never will -- it is the RS727 z-buffer,
 * reconstructed only to bound the argument. Whatever difference survives here
 * is the entire budget any amount of priority authoring is competing for.
 */
static void
raster_reference(
    const struct ToriDraw_Model* m,
    int n,
    const int* id_zbuf,
    int* out_pixels)
{
    for( int i = 0; i < n; i++ )
    {
        int const f = id_zbuf[i];
        out_pixels[i] = f < 0 ? 0 : (int)ToriDraw_Hsl16ToRgb(m->face_colors_a[f]);
    }
}

static void
score_accumulate(
    struct score* s,
    int w,
    int h,
    const int* id_painter,
    const int* z_painter,
    const int* id_zbuf,
    const int* z_zbuf)
{
    /* One raster unit of slack: coplanar seams and shared edges land on either
     * side of the same depth and must not read as errors. */
    int const slack = 1;

    for( int i = 0; i < w * h; i++ )
    {
        if( id_painter[i] < 0 )
            continue;
        s->covered++;
        if( id_painter[i] == id_zbuf[i] )
            continue;
        if( z_painter[i] > z_zbuf[i] + slack )
        {
            s->wrong++;
            s->depth_sum += z_painter[i] - z_zbuf[i];
        }
    }
}

/**
 * What the depth test actually changed, and whether it changed the right things.
 *
 * A raw "N pixels differ" is not evidence: the depth-tested kernels shade per
 * pixel where the stock ones shade per four, so a correct z-buffer moves some
 * pixels for reasons that have nothing to do with depth. What answers the
 * question is WHERE the differences land. The sort-error mask already says which
 * pixels are showing a surface behind the true nearest, so splitting the
 * differences by that mask separates "fixed something wrong" from "changed
 * something that was already right".
 */
struct diff_score
{
    long changed;          /* pixels where the two renders disagree */
    long changed_on_wrong; /* ...of those, ones the sort had wrong */
    long wrong_unchanged;  /* sort-wrong pixels the depth test left alone */
};

static void
diff_accumulate(
    struct diff_score* d,
    int n,
    const int* painter_pixels,
    const int* zbuf_pixels,
    const int* id_painter,
    const int* z_painter,
    const int* id_zbuf,
    const int* z_zbuf)
{
    for( int i = 0; i < n; i++ )
    {
        bool const changed = painter_pixels[i] != zbuf_pixels[i];
        bool const was_wrong = id_painter[i] >= 0 && id_painter[i] != id_zbuf[i] &&
                               z_painter[i] > z_zbuf[i] + 1;

        if( changed )
            d->changed++;
        if( changed && was_wrong )
            d->changed_on_wrong++;
        if( !changed && was_wrong )
            d->wrong_unchanged++;
    }
}

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage: %s --model FILE.ob3 [--model FILE.ob3 ...] --out OUT.bmp\n"
        "       [--angles N] [--yaw0 N] [--pitch N] [--zoom F] [--tile N]\n"
        "       [--focus X,Y,Z] [--radius R]\n"
        "       [--textures] [--ignore-priorities] [--stats] [--score]\n"
        "       [--zbuffer] [--compare] [--out-zbuffer F] [--diff-out F]\n",
        argv0);
}

int
main(int argc, char** argv)
{
    const char* model_paths[MAX_MODELS];
    int model_count = 0;
    const char* out_path = "model_view.bmp";
    int angles = 4;
    int yaw0 = 0;
    int pitches[MAX_PITCHES] = { 0 };
    int pitch_count = 1;
    double zoom = 1.0;
    int tile = 320;
    bool keep_textures = false;
    const char* lane_textures = NULL;
    bool ignore_priorities = false;
    bool stats = false;
    bool do_score = false;
    const char* score_path = NULL;
    const char* reference_path = NULL;
    bool zbuffered = false;
    bool compare = false;
    bool order_check = false;
    long order_check_diff = 0;
    long order_check_covered = 0;
    const char* zbuffer_path = "model_view_zbuffer.bmp";
    const char* diff_path = NULL;
    const char* cache_dir = NULL;
    const char* frames_arg = NULL;
    struct ToriDraw_Animation* anim = NULL;
    int* frame_ids = NULL;
    int frame_id_count = 0;
    /* The QBD is near-black geometry. On a black clear it reads as an empty
     * sheet, which is the same as producing nothing. */
    int background = 0x202430;
    bool have_focus = false;
    int focus[3] = { 0, 0, 0 };
    int focus_radius = 0;
    struct score total = { 0, 0, 0 };
    struct diff_score diff = { 0, 0, 0 };

    struct ToriDraw_Model* parts[MAX_MODELS];
    struct ToriDraw_Model* model = NULL;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_Scene* scene = NULL;
    int* pixels = NULL;
    int* tile_pixels = NULL;
    int* id_painter = NULL;
    int* z_painter = NULL;
    int* id_zbuf = NULL;
    int* z_zbuf = NULL;
    int* score_pixels = NULL;
    int* reference_pixels = NULL;
    int* reference_tile = NULL;
    int* zbuffer_pixels = NULL;
    int* zbuffer_tile = NULL;
    int* diff_pixels = NULL;
    int* order_tile = NULL;
    uint8_t model_flags_base = 0;
    int cols, rows, sheet_w, sheet_h;
    int extent, distance;
    int center_y;
    int rc = 1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--model") == 0 && i + 1 < argc )
        {
            if( model_count >= MAX_MODELS )
            {
                fprintf(stderr, "rs2012_model_view: too many --model\n");
                return 2;
            }
            model_paths[model_count++] = argv[++i];
        }
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_path = argv[++i];
        else if( strcmp(argv[i], "--angles") == 0 && i + 1 < argc )
            angles = atoi(argv[++i]);
        else if( strcmp(argv[i], "--yaw0") == 0 && i + 1 < argc )
            yaw0 = atoi(argv[++i]);
        else if( strcmp(argv[i], "--pitch") == 0 && i + 1 < argc )
        {
            const char* s = argv[++i];
            pitch_count = 0;
            while( *s && pitch_count < MAX_PITCHES )
            {
                pitches[pitch_count++] = (int)strtol(s, (char**)&s, 10);
                if( *s == ',' )
                    s++;
            }
        }
        else if( strcmp(argv[i], "--zoom") == 0 && i + 1 < argc )
            zoom = atof(argv[++i]);
        else if( strcmp(argv[i], "--tile") == 0 && i + 1 < argc )
            tile = atoi(argv[++i]);
        else if( strcmp(argv[i], "--textures") == 0 )
            keep_textures = true;
        else if( strcmp(argv[i], "--lane-textures") == 0 && i + 1 < argc )
        {
            /* Implies --textures: keeping the references is pointless without
             * a texture map, and loading a map is pointless without them. */
            lane_textures = argv[++i];
            keep_textures = true;
        }
        else if( strcmp(argv[i], "--ignore-priorities") == 0 )
            ignore_priorities = true;
        else if( strcmp(argv[i], "--stats") == 0 )
            stats = true;
        else if( strcmp(argv[i], "--score") == 0 )
            do_score = true;
        else if( strcmp(argv[i], "--score-out") == 0 && i + 1 < argc )
        {
            score_path = argv[++i];
            do_score = true;
        }
        else if( strcmp(argv[i], "--reference-out") == 0 && i + 1 < argc )
        {
            reference_path = argv[++i];
            do_score = true;
        }
        else if( strcmp(argv[i], "--zbuffer") == 0 )
            zbuffered = true;
        else if( strcmp(argv[i], "--order-check") == 0 )
            order_check = true;
        else if( strcmp(argv[i], "--compare") == 0 )
        {
            /* The comparison is only readable next to the sort-error mask, and
             * that comes from the scorer. */
            compare = true;
            do_score = true;
        }
        else if( strcmp(argv[i], "--out-zbuffer") == 0 && i + 1 < argc )
        {
            zbuffer_path = argv[++i];
            compare = true;
            do_score = true;
        }
        else if( strcmp(argv[i], "--diff-out") == 0 && i + 1 < argc )
        {
            diff_path = argv[++i];
            compare = true;
            do_score = true;
        }
        else if( strcmp(argv[i], "--focus") == 0 && i + 1 < argc )
        {
            if( sscanf(argv[++i], "%d,%d,%d", &focus[0], &focus[1], &focus[2]) != 3 )
            {
                usage(argv[0]);
                return 2;
            }
            have_focus = true;
        }
        else if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            cache_dir = argv[++i];
        else if( strcmp(argv[i], "--frames") == 0 && i + 1 < argc )
            frames_arg = argv[++i];
        else if( strcmp(argv[i], "--bg") == 0 && i + 1 < argc )
            background = (int)strtol(argv[++i], NULL, 16);
        else if( strcmp(argv[i], "--radius") == 0 && i + 1 < argc )
            focus_radius = atoi(argv[++i]);
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if( model_count == 0 || angles < 1 || angles > MAX_ANGLES || tile < 32 || zoom <= 0.0 )
    {
        usage(argv[0]);
        return 2;
    }

    /* The order check compares two DEPTH-TESTED renders, so the forward one has
     * to be depth tested. --compare already produces it; on its own the primary
     * sheet has to become it. */
    if( order_check && !compare )
        zbuffered = true;

    ToriDraw_Init();

    for( int i = 0; i < model_count; i++ )
    {
        parts[i] = load_ob3(model_paths[i]);
        if( !parts[i] )
            goto done;
    }

    if( model_count == 1 )
    {
        model = parts[0];
        parts[0] = NULL;
    }
    else
    {
        model = ToriDraw_ModelNewMerge(parts, model_count);
        if( !model )
        {
            fprintf(stderr, "rs2012_model_view: merge failed\n");
            goto done;
        }
    }

    if( ignore_priorities )
        drop_priorities(model);
    if( !keep_textures )
        strip_textures(model);

    /* Pose before framing: the animation moves the subject, so the centre and
     * the radius have to be measured on the pose that will actually be drawn.
     * Lighting is already baked into the face colours above and does not move
     * with the vertices, which is what lets the pose come after it. */
    if( cache_dir && frames_arg )
    {
        int capacity = 1;
        for( const char* c = frames_arg; *c; c++ )
            if( *c == ',' )
                capacity++;
        frame_ids = (int*)malloc((size_t)capacity * sizeof(int));
        if( !frame_ids )
            goto done;
        {
            const char* s2 = frames_arg;
            while( *s2 )
            {
                frame_ids[frame_id_count++] = (int)strtol(s2, (char**)&s2, 10);
                if( *s2 == ',' )
                    s2++;
            }
        }
        anim = load_animation_from_cache(cache_dir, frame_ids, frame_id_count);
        if( !anim )
            goto done;
        ToriDraw_ModelCaptureOriginalVertices(model);
        ToriDraw_ModelAnimateReset(model);
        if( anim->frames[0].length > 0 )
            ToriDraw_ModelAnimateFrame(model, anim->base, &anim->frames[0]);
    }

    recenter_model(model, have_focus ? focus : NULL);

    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;

    /* Actor regime: this lane's subjects are npcs. Lighting has to run before
     * the render or every face_colors_* stays zero and the sheet is black. */
    ToriDraw_ModelCalculateVertexNormals(model);
    ToriDraw_LightModelActor(hnd, 0, 0);

    if( stats )
        print_stats(model);

    /* Recentred, so the framing radius is just the cylinder's widest reach in
     * any rotation — the same number the cull uses. */
    center_y = 0;
    extent = focus_radius > 0
                 ? focus_radius
                 : (model->bounds_cylinder ? model->bounds_cylinder->min_z_depth_any_rotation
                                           : 512);
    if( extent < 1 )
        extent = 1;

    /* One column per yaw, one row per pitch: the sheet reads as an orbit. */
    cols = angles;
    rows = pitch_count;
    sheet_w = cols * tile;
    sheet_h = rows * tile;
    pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
    if( !pixels )
        goto done;
    if( score_path )
    {
        score_pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
        if( !score_pixels )
            goto done;
    }
    if( reference_path )
    {
        reference_pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
        reference_tile = (int*)calloc((size_t)tile * (size_t)tile, sizeof(int));
        if( !reference_pixels || !reference_tile )
            goto done;
    }
    if( compare )
    {
        zbuffer_pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
        zbuffer_tile = (int*)calloc((size_t)tile * (size_t)tile, sizeof(int));
        if( !zbuffer_pixels || !zbuffer_tile )
            goto done;
        if( diff_path )
        {
            diff_pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
            if( !diff_pixels )
                goto done;
        }
    }

    /* The adaptor already sets bit 0 on every model it converts, and bit 0 is
     * now the z-buffer opt-in, so the flag cannot be read as intent here. The
     * viewer drives it explicitly per pass and keeps the rest of the flags. */
    model_flags_base = (uint8_t)(model->flags & ~TORIDRAW_MODEL_FLAG_ZBUFFER);
    if( compare )
    {
        zbuffer_pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
        zbuffer_tile = (int*)calloc((size_t)tile * (size_t)tile, sizeof(int));
        if( !zbuffer_pixels || !zbuffer_tile )
            goto done;
    }
    if( diff_path )
    {
        diff_pixels = (int*)calloc((size_t)sheet_w * (size_t)sheet_h, sizeof(int));
        if( !diff_pixels )
            goto done;
    }
    if( order_check )
    {
        order_tile = (int*)calloc((size_t)tile * (size_t)tile, sizeof(int));
        if( !order_tile )
            goto done;
    }

    /* screen = world * scale / z, so the distance that makes `extent` fill
     * 90% of a half-tile is extent * scale / (0.45 * tile). */
    distance = (int)((double)extent * (double)TORIDRAW_PROJ_SCALE_DEFAULT /
                     (0.45 * (double)tile * zoom));
    if( distance < 100 )
        distance = 100;

    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_FULL | TORIDRAW_SCENE_DEPTH_16K |
            ((zbuffered || compare) ? TORIDRAW_SCENE_MODEL_ZBUFFER : 0u),
        TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    if( !scene )
        goto done;

    if( lane_textures && view_load_lane_textures(scene, lane_textures, true) == 0 )
        fprintf(stderr, "rs2012_model_view: textured faces will be skipped\n");

    /* The depth test is a per-model opt-in, so the switch is on the model, not
     * on the renderer. Kept as a base + a bit so --compare can flip it between
     * two rasters of one projection. */
    model_flags_base = (uint8_t)(model->flags & ~TORIDRAW_MODEL_FLAG_ZBUFFER);

    /* Each tile renders into its own origin-anchored buffer and is then
     * blitted. The AABB cull compares the projected box against 0..width, so a
     * tile whose x_center sits at sheet coordinates is culled outright. */
    tile_pixels = (int*)calloc((size_t)tile * (size_t)tile, sizeof(int));
    if( !tile_pixels )
        goto done;
    if( do_score )
    {
        size_t const n = (size_t)tile * (size_t)tile;
        id_painter = (int*)malloc(n * sizeof(int));
        z_painter = (int*)malloc(n * sizeof(int));
        id_zbuf = (int*)malloc(n * sizeof(int));
        z_zbuf = (int*)malloc(n * sizeof(int));
        if( !id_painter || !z_painter || !id_zbuf || !z_zbuf )
            goto done;
    }

    for( int view = 0; view < angles * pitch_count; view++ )
    {
        int const a = view % angles;
        int const p = view / angles;
        int const tx = a * tile;
        int const ty = p * tile;
        int const yaw = (yaw0 + (int)((long)a * 2048 / angles)) & 2047;
        int const pitch = pitches[p] & 2047;
        struct ToriDraw_ViewPort vp = {
            .width = tile,
            .height = tile,
            .stride = tile,
            .x_center = tile / 2,
            .y_center = tile / 2,
            .clip_left = 0,
            .clip_top = 0,
            .clip_right = tile,
            .clip_bottom = tile,
        };
        struct ToriDraw_Camera camera = {
            .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
            .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
            .near_plane_z = 50,
            .pitch = 0,
            .yaw = 0,
            .roll = 0,
        };
        struct ToriDraw_Position pos = {
            .x = 0,
            .y = -center_y,
            .z = distance,
            .pitch = pitch,
            .yaw = yaw,
            .roll = 0,
        };
        int cull;

        for( int i2 = 0; i2 < tile * tile; i2++ )
            tile_pixels[i2] = background;
        cull = ToriDraw_RenderModel1Project(hnd, scene, &pos, &vp, &camera);
        if( cull != TORIDRAW_CULL_VISIBLE )
            fprintf(stderr, "rs2012_model_view: yaw %d culled (code %d)\n", yaw, cull);
        else if( ToriDraw_RenderModel2SortFaces(hnd, scene) <= 0 )
            fprintf(stderr, "rs2012_model_view: yaw %d sorted 0 faces\n", yaw);
        else
        {
            /* Scored off the projection BEFORE the raster runs: the raster
             * family is free to rewrite the screen-vertex scratch it consumes,
             * and a score taken afterwards is a score of whatever it left. */
            if( do_score )
            {
                raster_id_z(
                    model,
                    scene,
                    ToriDraw_FaceOrder(scene),
                    ToriDraw_FaceOrderCount(scene),
                    tile,
                    tile,
                    vp.x_center,
                    vp.y_center,
                    id_painter,
                    z_painter,
                    id_zbuf,
                    z_zbuf);
                score_accumulate(&total, tile, tile, id_painter, z_painter, id_zbuf, z_zbuf);

                if( reference_pixels )
                {
                    raster_reference(model, tile * tile, id_zbuf, reference_tile);
                    for( int row = 0; row < tile; row++ )
                        memcpy(
                            &reference_pixels[(size_t)(ty + row) * (size_t)sheet_w + tx],
                            &reference_tile[(size_t)row * (size_t)tile],
                            (size_t)tile * sizeof(int));
                }

                /* The mask is the reason the score is actionable: a percentage
                 * says the sort is wrong, the mask says which feature. */
                if( score_pixels )
                    for( int row = 0; row < tile; row++ )
                        for( int col = 0; col < tile; col++ )
                        {
                            int const at = row * tile + col;
                            int const to = (ty + row) * sheet_w + tx + col;
                            if( id_painter[at] < 0 )
                                score_pixels[to] = 0;
                            else if( id_painter[at] != id_zbuf[at] &&
                                     z_painter[at] > z_zbuf[at] + 1 )
                                score_pixels[to] = 0xFF0000;
                            else
                                score_pixels[to] = 0x203040;
                        }
            }

            /* Without --compare the primary sheet is whichever family was
             * asked for; with it the primary is always the painter's, so the
             * two sheets sit side by side and the diff below means something. */
            model->flags = (uint8_t)(model_flags_base |
                                     ((zbuffered && !compare)
                                          ? TORIDRAW_MODEL_FLAG_ZBUFFER
                                          : 0u));
            ToriDraw_RenderModel3Raster(scene, &vp, &camera, tile_pixels, false);

            if( compare )
            {
                /* Reproject before the second raster rather than reusing the
                 * scratch: the raster families are allowed to write over the
                 * screen-vertex arrays they consume, so a second pass off the
                 * first pass's leftovers would not be the same picture. */
                for( int i2 = 0; i2 < tile * tile; i2++ )
                    zbuffer_tile[i2] = background;
                model->flags = (uint8_t)(model_flags_base | TORIDRAW_MODEL_FLAG_ZBUFFER);
                if( ToriDraw_RenderModel1Project(hnd, scene, &pos, &vp, &camera) ==
                        TORIDRAW_CULL_VISIBLE &&
                    ToriDraw_RenderModel2SortFaces(hnd, scene) > 0 )
                    ToriDraw_RenderModel3Raster(scene, &vp, &camera, zbuffer_tile, false);

                diff_accumulate(
                    &diff,
                    tile * tile,
                    tile_pixels,
                    zbuffer_tile,
                    id_painter,
                    z_painter,
                    id_zbuf,
                    z_zbuf);

                for( int row = 0; row < tile; row++ )
                {
                    memcpy(
                        &zbuffer_pixels[(size_t)(ty + row) * (size_t)sheet_w + tx],
                        &zbuffer_tile[(size_t)row * (size_t)tile],
                        (size_t)tile * sizeof(int));
                    if( !diff_pixels )
                        continue;
                    for( int col = 0; col < tile; col++ )
                    {
                        int const at = row * tile + col;
                        int const to = (ty + row) * sheet_w + tx + col;
                        bool const changed = tile_pixels[at] != zbuffer_tile[at];
                        bool const was_wrong = id_painter[at] >= 0 &&
                                               id_painter[at] != id_zbuf[at] &&
                                               z_painter[at] > z_zbuf[at] + 1;

                        /* Green: a pixel the sort had wrong and the depth test
                         * changed. Red: changed where the sort was already
                         * right. Blue: still wrong after the depth test — the
                         * only colour that should be absent. */
                        if( changed && was_wrong )
                            diff_pixels[to] = 0x00C000;
                        else if( changed )
                            diff_pixels[to] = 0xC00000;
                        else if( was_wrong )
                            diff_pixels[to] = 0x0000FF;
                        else
                            diff_pixels[to] = id_painter[at] < 0 ? 0 : 0x203040;
                    }
                }
            }
        }

        if( order_check )
        {
            /* Same projection, same faces, opposite order. The depth test is
             * the only thing that can make the two agree, so any pixel that
             * differs is either a translucent face (which composites in order
             * by design) or a defect in the kernels. */
            int* order;
            int order_count;

            memset(order_tile, 0, (size_t)tile * (size_t)tile * sizeof(int));
            model->flags = (uint8_t)(model_flags_base | TORIDRAW_MODEL_FLAG_ZBUFFER);
            if( ToriDraw_RenderModel1Project(hnd, scene, &pos, &vp, &camera) ==
                    TORIDRAW_CULL_VISIBLE &&
                (order_count = ToriDraw_RenderModel2SortFaces(hnd, scene)) > 0 )
            {
                int* forward = (int*)malloc((size_t)order_count * sizeof(int));

                if( forward )
                {
                    order = ToriDraw_FaceOrder(scene);
                    memcpy(forward, order, (size_t)order_count * sizeof(int));
                    for( int i = 0; i < order_count; i++ )
                        order[i] = forward[order_count - 1 - i];
                    ToriDraw_RenderModel3Raster(scene, &vp, &camera, order_tile, false);
                    free(forward);

                    for( int i = 0; i < tile * tile; i++ )
                    {
                        int const forward_pixel =
                            compare ? zbuffer_tile[i] : tile_pixels[i];

                        if( forward_pixel || order_tile[i] )
                            order_check_covered++;
                        if( forward_pixel != order_tile[i] )
                            order_check_diff++;
                    }
                }
            }
        }

        for( int row = 0; row < tile; row++ )
            memcpy(
                &pixels[(size_t)(ty + row) * (size_t)sheet_w + tx],
                &tile_pixels[(size_t)row * (size_t)tile],
                (size_t)tile * sizeof(int));
    }

    model->flags = model_flags_base;

    bmp_write_file(out_path, pixels, sheet_w, sheet_h);
    printf("rs2012_model_view: wrote %s (%dx%d, %d angles)\n", out_path, sheet_w, sheet_h, angles);
    if( score_pixels )
    {
        bmp_write_file(score_path, score_pixels, sheet_w, sheet_h);
        printf("rs2012_model_view: wrote %s (sort-error mask)\n", score_path);
    }
    if( reference_pixels )
    {
        bmp_write_file(reference_path, reference_pixels, sheet_w, sheet_h);
        printf("rs2012_model_view: wrote %s (z-buffer reference)\n", reference_path);
    }
    if( zbuffer_pixels )
    {
        bmp_write_file(zbuffer_path, zbuffer_pixels, sheet_w, sheet_h);
        printf("rs2012_model_view: wrote %s (depth-tested)\n", zbuffer_path);
    }
    if( diff_pixels )
    {
        bmp_write_file(diff_path, diff_pixels, sheet_w, sheet_h);
        printf(
            "rs2012_model_view: wrote %s (green=fixed, red=changed-but-was-right, "
            "blue=still wrong)\n",
            diff_path);
    }
    if( do_score )
        printf(
            "sort score: %ld/%ld pixels behind the true surface (%.3f%%), "
            "mean depth error %.1f\n",
            total.wrong,
            total.covered,
            total.covered ? 100.0 * (double)total.wrong / (double)total.covered : 0.0,
            total.wrong ? (double)total.depth_sum / (double)total.wrong : 0.0);
    if( compare )
        printf(
            "depth test: %ld pixels changed, %ld of them on sort-wrong pixels "
            "(%.1f%% of the %ld wrong); %ld wrong pixels left untouched\n",
            diff.changed,
            diff.changed_on_wrong,
            total.wrong ? 100.0 * (double)diff.changed_on_wrong / (double)total.wrong : 0.0,
            total.wrong,
            diff.wrong_unchanged);
    if( order_check )
        printf(
            "order check: %ld/%ld pixels differ when the face order is reversed "
            "(%.3f%%)\n",
            order_check_diff,
            order_check_covered,
            order_check_covered
                ? 100.0 * (double)order_check_diff / (double)order_check_covered
                : 0.0);
    rc = 0;

done:
    ToriDraw_SceneFree(scene);
    free(pixels);
    free(tile_pixels);
    free(id_painter);
    free(z_painter);
    free(id_zbuf);
    free(z_zbuf);
    free(score_pixels);
    free(frame_ids);
    ToriDraw_AnimationFree(anim);
    free(reference_pixels);
    free(reference_tile);
    free(zbuffer_pixels);
    free(zbuffer_tile);
    free(diff_pixels);
    free(order_tile);
    ToriDraw_ModelFree(model);
    for( int i = 0; i < model_count; i++ )
        ToriDraw_ModelFree(parts[i]);
    return rc;
}
