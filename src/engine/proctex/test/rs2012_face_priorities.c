/*
 * Face-priority authoring for models imported from a z-buffered client.
 *
 * ToriDraw resolves visibility with a per-face painter's sort, not a depth
 * buffer. A model authored against RS727's z-buffer carries no draw order to
 * recover: its surfaces interleave freely, and a centroid depth sort cannot
 * keep a thin decorative plate lying on a neck in front of the neck, or a claw
 * ring in front of the arm it grips. Face render priorities are the only lever
 * the sorter exposes, and there are exactly twelve of them.
 *
 * WHAT THE SORTER DOES WITH THEM (toridraw_render.u.c sort_face_draw_order),
 * because it decides everything below. Emitted back to front:
 *
 *     flexible faces deeper than avg(prio 1,2)
 *     prio 0, 1, 2                <- fixed band, each internally depth sorted
 *     flexible faces deeper than avg(prio 3,4)
 *     prio 3, 4
 *     flexible faces deeper than avg(prio 6,8)
 *     prio 5, 6, 7, 8, 9
 *     remaining flexible faces
 *
 * Priorities 0..9 are ten HARD bands: every face in band N paints over every
 * face in band N-1 whatever their depths, and inside a band the depth sort
 * still runs. Priorities 10 and 11 are the FLEXIBLE band, purely depth sorted
 * and spliced into the fixed run at three depth averages.
 *
 * Two rules follow, and this tool exists to enforce them:
 *
 *   1. A feature lives in exactly one band. Splitting one surface across two
 *      bands does not refine its order, it forbids its own halves from
 *      interleaving, so the far half paints over the near half. Bands separate
 *      features; depth orders faces within one.
 *
 *   2. A band separation is a claim that holds from EVERY camera angle. Two
 *      features that swap places as the model turns (a left and a right wing)
 *      must share a band, or one of them is wrong at half the angles.
 *
 * So the bands are not guessed from names or bounding boxes -- they are
 * measured, and the thing measured is not "which feature is in front".
 * Asking that merges the whole dragon into one band, and rightly: a spike
 * standing out of a neck is in front of the neck from one side and behind it
 * from the other, so no band can hold it. Almost every pair of features on a
 * closed creature answers "both".
 *
 * The question that pays is narrower. The depth sort already resolves the
 * overwhelming majority of pixels correctly. A band is worth spending only
 * where it FIXES pixels the depth sort gets wrong, and every band also BREAKS
 * the pixels where the loser legitimately wins. So each ordered pair is scored
 * by rendering the model from a sphere of viewpoints and counting, per pixel:
 *
 *   fixable[A][B]   A should win here, B is what the depth sort paints last
 *   breakable[A][B] B should win here and does; putting A over B destroys it
 *
 * net(A over B) = fixable - breakable. What that number must NOT be used for
 * is deciding the pair on its own. A band is not a pairwise promise: putting A
 * one band above B puts it above every other feature in B's band too,
 * including the 2,258-face body it was never compared against. Authoring pair
 * by pair reads as a win on every pair and lands a small spike in front of the
 * whole dragon -- measured here at 4.5% wrong pixels going to 7.4%, with the
 * mean depth error of an error rising from 9 units to 80.
 *
 * So the assignment is solved globally: choose a band per feature to maximise
 *
 *     sum over pairs with band(A) > band(B) of net(A over B)
 *
 * by hill climbing from "everything in band 0", which is exactly today's pure
 * depth sort and therefore a baseline the result cannot score below. A pair
 * that swaps as the model turns scores its own conflict away and is never
 * separated -- rule 2 falls out of the arithmetic instead of being imposed on
 * top of it.
 *
 * The measurement depends on the assignment (it asks what the sort paints
 * today) and the assignment depends on the measurement, so the two alternate
 * until the bands stop moving. Round one measures the pure depth sort.
 *
 * The input .ob3 is never written. Analysis runs over all inputs together --
 * an npc's model1 and model2 are merged before they are drawn, so their bands
 * have to be chosen in one coordinate system -- and each output gets its own
 * slice of the result.
 *
 * Build and run:
 *   make -C src rs2012-face-priorities
 *   src/build/rs2012_face_priorities --in head.ob3 --in body.ob3 --report \
 *       --out head.prio.ob3 --out body.prio.ob3
 */

#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/model.h"
#include "filelist.h"

#include "engine/toridraw_animation_from_rscache.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/torirs_model_from_rscache.h"
#include "engine/torirs_types.h"

#include <toridraw.h>

#include <math.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUTS 16
#define MAX_FEATURES 2048
#define HARD_BANDS 10
/* Priorities 10 and 11 are the sorter's flexible band: depth sorted and
 * spliced into the fixed run, not pinned in front of it. */
#define FLEX_BAND 10

/* -std=c11 hides M_PI; the view sweep is the only thing that wants it. */
#define PRIO_PI 3.14159265358979323846

/* ToriDraw_ModelDropNonSdTextures is the only thing in the model adaptor that
 * reaches a CacheProvider, and this tool never calls it. */
bool
CacheProvider_TextureIsSd(struct CacheProvider* provider, int texture_id);
bool
CacheProvider_TextureIsSd(struct CacheProvider* provider, int texture_id)
{
    (void)provider;
    (void)texture_id;
    return true;
}

/* --------------------------------------------------------------- inputs -- */

struct input
{
    const char* in_path;
    const char* out_path;
    struct RSCache_Model* model;
    struct RSCache_ModelProvenance* provenance;
    int vertex_base;
    int face_base;
};

/* Concatenated geometry across every input, in input order, so a face index
 * here maps back to (input, local face) by the recorded bases. */
struct geometry
{
    int vertex_count;
    int face_count;
    int* vx;
    int* vy;
    int* vz;
    int* fa;
    int* fb;
    int* fc;
    int* face_feature;
};

struct feature
{
    int face_count;
    int band;
};

/** What the sort gets wrong, and how much of it a band could ever reach. */
struct tally
{
    long covered;
    long wrong_within;  /* one feature sorting wrongly against itself */
    long wrong_between; /* two features: the part priorities can address */
};

/* ------------------------------------------------------------------ io --- */

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

/* ----------------------------------------------------------- union find -- */

static int
uf_find(int* parent, int x)
{
    while( parent[x] != x )
    {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

static void
uf_union(int* parent, int a, int b)
{
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if( a != b )
        parent[b] = a;
}

/* --------------------------------------------------------- segmentation -- */

struct weld_key
{
    int64_t packed;
    int vertex;
};

static int
cmp_weld_key(const void* a, const void* b)
{
    int64_t ka = ((const struct weld_key*)a)->packed;
    int64_t kb = ((const struct weld_key*)b)->packed;
    return ka < kb ? -1 : (ka > kb ? 1 : 0);
}

/**
 * Split the geometry into connected features.
 *
 * Faces sharing a vertex index are one surface -- that is what "one feature"
 * meant to whoever built it. `weld` additionally unions vertices at identical
 * coordinates, because an imported model routinely duplicates a seam's
 * vertices and the halves must not be able to land in different bands.
 */
static int
segment(struct geometry* g, bool weld)
{
    int* parent = (int*)malloc((size_t)g->vertex_count * sizeof(int));
    int* remap = (int*)malloc((size_t)g->vertex_count * sizeof(int));
    int count = 0;

    if( !parent || !remap )
    {
        free(parent);
        free(remap);
        return 0;
    }
    for( int v = 0; v < g->vertex_count; v++ )
        parent[v] = v;

    if( weld )
    {
        struct weld_key* keys =
            (struct weld_key*)malloc((size_t)g->vertex_count * sizeof(struct weld_key));
        if( keys )
        {
            for( int v = 0; v < g->vertex_count; v++ )
            {
                int64_t x = (int64_t)(g->vx[v] + 32768);
                int64_t y = (int64_t)(g->vy[v] + 32768);
                int64_t z = (int64_t)(g->vz[v] + 32768);
                keys[v].packed = (x << 34) | (y << 17) | z;
                keys[v].vertex = v;
            }
            qsort(keys, (size_t)g->vertex_count, sizeof(struct weld_key), cmp_weld_key);
            for( int i = 1; i < g->vertex_count; i++ )
                if( keys[i].packed == keys[i - 1].packed )
                    uf_union(parent, keys[i].vertex, keys[i - 1].vertex);
            free(keys);
        }
    }

    for( int f = 0; f < g->face_count; f++ )
    {
        uf_union(parent, g->fa[f], g->fb[f]);
        uf_union(parent, g->fb[f], g->fc[f]);
    }

    for( int v = 0; v < g->vertex_count; v++ )
        remap[v] = -1;
    for( int f = 0; f < g->face_count; f++ )
    {
        int root = uf_find(parent, g->fa[f]);
        if( remap[root] < 0 )
            remap[root] = count++;
        g->face_feature[f] = remap[root];
    }

    free(parent);
    free(remap);
    return count;
}

/* --------------------------------------------------------------- poses -- */

/**
 * The model in one pose: the bind pose, or one frame of an animation.
 *
 * Bands are a property of the MODEL, not of a pose, but every measurement that
 * chooses them is taken in some pose — and a boss spends almost none of its
 * screen time in the bind pose. The Queen Black Dragon's wake sequence lifts a
 * head that starts folded against the neck: features that never overlap in the
 * bind pose spend the whole animation on top of each other, and a band solved
 * from the bind pose alone has no opinion about them.
 *
 * So a pose is just another axis to sample, orthogonal to camera angle, and
 * the fix/break counters accumulate across all of them. A band has to earn its
 * pixels over the whole sequence.
 */
struct poses
{
    struct ToriDraw_Model* model;     /* merged, animatable, owns the vertices */
    struct ToriDraw_Animation* anim;  /* NULL for bind pose only */
    int* frames;                      /* animation frame indices to sample */
    int frame_count;
    int bind_extent;                  /* the yardstick apply_pose sanity-checks against */
    int rejected;                     /* poses dropped as decode wreckage */
};

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

/** Read a lane `<name>.memberpack` to learn how many members its archive has. */
static int
memberpack_count(const char* archive_path)
{
    char path[1024];
    const char* dot = strrchr(archive_path, '.');
    size_t stem = dot ? (size_t)(dot - archive_path) : strlen(archive_path);
    FILE* f;
    int count = 0;
    int c;

    if( stem + 12 >= sizeof(path) )
        return 0;
    memcpy(path, archive_path, stem);
    memcpy(path + stem, ".memberpack", 12);

    f = fopen(path, "rb");
    if( !f )
        return 0;
    while( (c = fgetc(f)) != EOF )
        if( c == '\n' )
            count++;
    fclose(f);
    return count;
}

/**
 * Build the animation the poses come from.
 *
 * The frames live in one multi-member archive (the lane's `.anim`), the rigging
 * in a separate framemap file — the dat2 split. Both arrive as staged files
 * rather than through a cache, so this decodes them directly.
 */
static struct ToriDraw_Animation*
load_animation(
    const char* anim_path,
    const char* framemap_path,
    const int* frame_indices,
    int frame_count,
    int frame_codec,
    int framemap_codec)
{
    long anim_size = 0, map_size = 0;
    uint8_t* anim_bytes = read_file(anim_path, &anim_size);
    uint8_t* map_bytes = read_file(framemap_path, &map_size);
    struct RSCache_Dat2Framemap* framemap = NULL;
    struct RSCache_FileList* files = NULL;
    struct RSCache_Dat2Frame** frames = NULL;
    int* delays = NULL;
    struct ToriDraw_Animation* out = NULL;
    int members;

    if( !anim_bytes || !map_bytes )
    {
        fprintf(stderr, "rs2012_face_priorities: cannot read the animation files\n");
        goto done;
    }
    members = memberpack_count(anim_path);
    if( members <= 0 )
    {
        fprintf(
            stderr,
            "rs2012_face_priorities: no %s.memberpack, cannot size the archive\n",
            anim_path);
        goto done;
    }

    files = RSCache_FileListNewFromDecode((char*)anim_bytes, (int)anim_size, members);
    if( !files )
    {
        fprintf(stderr, "rs2012_face_priorities: cannot split the animation archive\n");
        goto done;
    }

    /* The framemap's id is not a free choice: every frame names the framemap it
     * was built against, and the frame decoder asserts the two agree. Read it
     * off the first frame the sequence uses rather than being told it, so the
     * caller passes a path and nothing else can be out of step. */
    if( frame_count <= 0 || frame_indices[0] < 0 || frame_indices[0] >= files->file_count )
    {
        fprintf(stderr, "rs2012_face_priorities: no usable frame to read the rig from\n");
        goto done;
    }
    {
        int const first = frame_indices[0];
        int const framemap_id = RSCache_Dat2FrameFramemapIdFromFileCodec(
            files->files[first], files->file_sizes[first], frame_codec);
        framemap = RSCache_Dat2FramemapNewDecodeCodec(
            framemap_id, (char*)map_bytes, (int)map_size, framemap_codec);
    }
    if( !framemap )
    {
        fprintf(stderr, "rs2012_face_priorities: cannot decode %s\n", framemap_path);
        goto done;
    }

    frames = (struct RSCache_Dat2Frame**)calloc((size_t)frame_count, sizeof(*frames));
    delays = (int*)malloc((size_t)frame_count * sizeof(int));
    if( !frames || !delays )
        goto done;

    for( int i = 0; i < frame_count; i++ )
    {
        int const idx = frame_indices[i];
        if( idx < 0 || idx >= files->file_count )
        {
            fprintf(
                stderr,
                "rs2012_face_priorities: frame %d outside the archive's %d members\n",
                idx,
                files->file_count);
            goto done;
        }
        frames[i] = RSCache_Dat2FrameNewDecodeCodec(
            idx, framemap, files->files[idx], files->file_sizes[idx], frame_codec);
        if( !frames[i] )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot decode frame %d\n", idx);
            goto done;
        }
        delays[i] = 1;
    }

    out = ToriDraw_AnimationFromRSCache(
        framemap, (struct RSCache_Dat2Frame const* const*)frames, delays, frame_count, 0);
    if( !out )
        fprintf(stderr, "rs2012_face_priorities: cannot assemble the animation\n");

done:
    if( frames )
        for( int i = 0; i < frame_count; i++ )
            RSCache_Dat2FrameFree(frames[i]);
    free(frames);
    free(delays);
    RSCache_FileListFree(files);
    RSCache_Dat2FramemapFree(framemap);
    free(anim_bytes);
    free(map_bytes);
    return out;
}

/**
 * Put the geometry into pose `p` and re-frame it.
 *
 * Recentring per pose rather than once is what keeps a moving subject in shot:
 * the wake sequence swings the head through most of the model's own height, and
 * a centre fixed on the bind pose walks it out of the raster half way through.
 * Each pose is measured in its own frame, which is right — the pose is the
 * subject, and the camera is sampled separately.
 */
static bool
apply_pose(
    struct poses* poses,
    struct geometry* g,
    int pose_index,
    int res,
    int scale,
    int* out_distance)
{
    const struct ToriDraw_Model* m = poses->model;
    int min_v[3] = { 1 << 30, 1 << 30, 1 << 30 };
    int max_v[3] = { -(1 << 30), -(1 << 30), -(1 << 30) };
    int center[3];
    long radius2 = 0;
    int extent;

    ToriDraw_ModelAnimateReset(poses->model);
    if( pose_index > 0 && poses->anim )
        ToriDraw_ModelAnimateFrame(
            poses->model, poses->anim->base, &poses->anim->frames[pose_index - 1]);

    for( int v = 0; v < g->vertex_count; v++ )
    {
        g->vx[v] = m->vertices_x[v];
        g->vy[v] = m->vertices_y[v];
        g->vz[v] = m->vertices_z[v];
    }
    for( int v = 0; v < g->vertex_count; v++ )
    {
        int c[3] = { g->vx[v], g->vy[v], g->vz[v] };
        for( int k = 0; k < 3; k++ )
        {
            if( c[k] < min_v[k] ) min_v[k] = c[k];
            if( c[k] > max_v[k] ) max_v[k] = c[k];
        }
    }
    for( int k = 0; k < 3; k++ )
        center[k] = (min_v[k] + max_v[k]) / 2;
    for( int v = 0; v < g->vertex_count; v++ )
    {
        long dx = g->vx[v] -= center[0];
        long dy = g->vy[v] -= center[1];
        long dz = g->vz[v] -= center[2];
        long d2 = dx * dx + dy * dy + dz * dz;
        if( d2 > radius2 )
            radius2 = d2;
    }
    extent = (int)sqrt((double)radius2) + 1;
    *out_distance = (int)((double)extent * scale / (0.45 * res)) + extent;

    /* Refuse a pose that is not a pose.
     *
     * A frame whose rig does not match the model it is applied to does not
     * fail loudly — it produces geometry, just not the right geometry, and the
     * usual shape of the wreckage is every vertex on the pivot. Measured, that
     * pose contributes a solid blob of feature-vs-feature "errors" that are
     * pure decode artefact, and they would move real bands. A tenth of the
     * bind pose's reach is far below anything an animation does and far above
     * a collapse. */
    if( poses->bind_extent > 0 && extent * 10 < poses->bind_extent )
        return false;
    if( pose_index == 0 )
        poses->bind_extent = extent;
    return true;
}

/* ---------------------------------------------------------- measurement -- */

/**
 * One sampled camera. Only the rotation matters: any orthonormal set covering
 * the sphere answers "does this pair ever resolve the other way", and the
 * sorter's own projection is not needed to ask that.
 */
struct view
{
    double m[9]; /* row-major model->camera rotation */
};

static void
view_make(struct view* v, double yaw, double pitch)
{
    double cy = cos(yaw), sy = sin(yaw);
    double cp = cos(pitch), sp = sin(pitch);

    /* yaw about Y, then pitch about X. */
    v->m[0] = cy;       v->m[1] = 0.0; v->m[2] = sy;
    v->m[3] = sp * sy;  v->m[4] = cp;  v->m[5] = -sp * cy;
    v->m[6] = -cp * sy; v->m[7] = sp;  v->m[8] = cp * cy;
}

struct raster
{
    int w, h;
    int* near_feature;  /* z-buffer winner: what the pixel should show */
    int* near_z;
    int* paint_feature; /* depth-sort winner: what the pixel does show */
    int* paint_z;
    int* scratch_z;
    int* scratch_stamp;
    int stamp;
    int* touched;
    int touched_count;
    int* sx;
    int* sy;
    int* sz;
    int* face_depth;
    int* face_band;
    int* face_order;
};

/**
 * Rasterize one triangle, calling back per covered pixel with its interpolated
 * depth. Faces whose screen winding is back-facing are dropped exactly as
 * ToriDraw drops them (toridraw_winding_front_facing: winding > 0), because a
 * face the client never draws must not constrain a band.
 */
#define TRI_FOR_EACH_PIXEL(r, f, PIXEL_BODY)                                                       \
    do                                                                                             \
    {                                                                                              \
        int const _ia = g->fa[f], _ib = g->fb[f], _ic = g->fc[f];                                  \
        long const _ax = (r)->sx[_ia], _ay = (r)->sy[_ia], _az = (r)->sz[_ia];                     \
        long const _bx = (r)->sx[_ib], _by = (r)->sy[_ib], _bz = (r)->sz[_ib];                     \
        long const _cx = (r)->sx[_ic], _cy = (r)->sy[_ic], _cz = (r)->sz[_ic];                     \
        long const _winding = (_ax - _bx) * (_cy - _by) - (_ay - _by) * (_cx - _bx);               \
        if( _winding > 0 )                                                                         \
        {                                                                                          \
            long const _area = (_bx - _ax) * (_cy - _ay) - (_by - _ay) * (_cx - _ax);              \
            int _min_x = (int)(_ax < _bx ? (_ax < _cx ? _ax : _cx) : (_bx < _cx ? _bx : _cx));     \
            int _max_x = (int)(_ax > _bx ? (_ax > _cx ? _ax : _cx) : (_bx > _cx ? _bx : _cx));     \
            int _min_y = (int)(_ay < _by ? (_ay < _cy ? _ay : _cy) : (_by < _cy ? _by : _cy));     \
            int _max_y = (int)(_ay > _by ? (_ay > _cy ? _ay : _cy) : (_by > _cy ? _by : _cy));     \
            if( _area != 0 )                                                                       \
            {                                                                                      \
                if( _min_x < 0 ) _min_x = 0;                                                       \
                if( _min_y < 0 ) _min_y = 0;                                                       \
                if( _max_x >= (r)->w ) _max_x = (r)->w - 1;                                        \
                if( _max_y >= (r)->h ) _max_y = (r)->h - 1;                                        \
                for( int _y = _min_y; _y <= _max_y; _y++ )                                         \
                    for( int _x = _min_x; _x <= _max_x; _x++ )                                     \
                    {                                                                              \
                        long _w0 = (_bx - _ax) * (_y - _ay) - (_by - _ay) * (_x - _ax);            \
                        long _w1 = (_cx - _bx) * (_y - _by) - (_cy - _by) * (_x - _bx);            \
                        long _w2 = (_ax - _cx) * (_y - _cy) - (_ay - _cy) * (_x - _cx);            \
                        int at, z;                                                                 \
                        if( _area > 0 )                                                            \
                        {                                                                          \
                            if( _w0 < 0 || _w1 < 0 || _w2 < 0 )                                    \
                                continue;                                                          \
                        }                                                                          \
                        else if( _w0 > 0 || _w1 > 0 || _w2 > 0 )                                   \
                            continue;                                                              \
                        z = (int)(((double)_w1 * _az + (double)_w2 * _bz + (double)_w0 * _cz) /    \
                                  (double)_area);                                                  \
                        at = _y * (r)->w + _x;                                                     \
                        (void)at;                                                                  \
                        (void)z;                                                                   \
                        PIXEL_BODY                                                                 \
                    }                                                                              \
            }                                                                                      \
        }                                                                                          \
    } while( 0 )

static void
project_view(
    const struct geometry* g,
    const struct view* v,
    struct raster* r,
    int distance,
    int scale)
{
    for( int i = 0; i < g->vertex_count; i++ )
    {
        double x = g->vx[i], y = g->vy[i], z = g->vz[i];
        double cx = v->m[0] * x + v->m[1] * y + v->m[2] * z;
        double cy = v->m[3] * x + v->m[4] * y + v->m[5] * z;
        double cz = v->m[6] * x + v->m[7] * y + v->m[8] * z + distance;

        if( cz < 1.0 )
            cz = 1.0;
        r->sx[i] = (int)(cx * scale / cz) + r->w / 2;
        r->sy[i] = (int)(cy * scale / cz) + r->h / 2;
        r->sz[i] = (int)cz;
    }
}

/* The draw order ToriDraw produces for a model whose priorities are all in the
 * hard range: band ascending, and inside a band the depth buckets walked from
 * the far end. Ties keep face order, as the buckets do. */
static __thread const int* g_sort_depth;
static __thread const int* g_sort_band;

static int
cmp_face_order(const void* a, const void* b)
{
    int fa = *(const int*)a, fb = *(const int*)b;
    if( g_sort_band[fa] != g_sort_band[fb] )
        return g_sort_band[fa] - g_sort_band[fb];
    if( g_sort_depth[fa] != g_sort_depth[fb] )
        return g_sort_depth[fb] - g_sort_depth[fa];
    return fa - fb;
}

/**
 * Score every ordered feature pair for one view.
 *
 * Three passes over the same projection:
 *   1. z-buffer      -- which feature the pixel SHOULD show
 *   2. painter       -- which feature the depth sort actually leaves there
 *   3. per feature   -- every feature that reaches the pixel, and how deep
 *
 * Pass 3 replays each feature into a private depth buffer rather than counting
 * per face, so a relation is weighted by the pixels it covers and not by how
 * finely the loser happens to be tessellated.
 *
 * The verdict per (pixel, covering feature F), against the true winner T:
 *   F is in front of T          -- impossible, T is the z-buffer minimum
 *   F behind T and F is painted -- a visible error; T over F would fix it
 *   F behind T and T is painted -- correct today; F over T would break it
 */
static void
measure_view(
    const struct geometry* g,
    struct raster* r,
    int feature_count,
    const int* feature_face_offset,
    const int* feature_faces,
    const int* band,
    long* fixable,
    long* breakable,
    struct tally* tally,
    int slack)
{
    for( int i = 0; i < r->w * r->h; i++ )
    {
        r->near_feature[i] = -1;
        r->paint_feature[i] = -1;
        r->near_z[i] = 1 << 30;
    }

    for( int f = 0; f < g->face_count; f++ )
    {
        int const feat = g->face_feature[f];
        TRI_FOR_EACH_PIXEL(r, f, {
            if( z < r->near_z[at] )
            {
                r->near_z[at] = z;
                r->near_feature[at] = feat;
            }
        });
    }

    for( int f = 0; f < g->face_count; f++ )
    {
        r->face_depth[f] = (r->sz[g->fa[f]] + r->sz[g->fb[f]] + r->sz[g->fc[f]]) / 3;
        r->face_band[f] = band[g->face_feature[f]];
        r->face_order[f] = f;
    }
    g_sort_depth = r->face_depth;
    g_sort_band = r->face_band;
    qsort(r->face_order, (size_t)g->face_count, sizeof(int), cmp_face_order);

    for( int i = 0; i < g->face_count; i++ )
    {
        int const f = r->face_order[i];
        int const feat = g->face_feature[f];
        TRI_FOR_EACH_PIXEL(r, f, {
            r->paint_feature[at] = feat;
            r->paint_z[at] = z;
        });
    }

    /* The ceiling. A pixel whose painted surface is behind the true one is an
     * error, but only the ones where the two belong to DIFFERENT features can
     * ever be reached by a band -- rule 1 forbids splitting a feature across
     * two, so a surface sorting wrongly against itself is out of scope for
     * priorities entirely. Worth printing before anyone spends a day on it. */
    for( int i = 0; i < r->w * r->h; i++ )
    {
        if( r->paint_feature[i] < 0 || r->paint_z[i] <= r->near_z[i] + slack )
            continue;
        if( r->paint_feature[i] == r->near_feature[i] )
            tally->wrong_within++;
        else
            tally->wrong_between++;
    }
    for( int i = 0; i < r->w * r->h; i++ )
        if( r->paint_feature[i] >= 0 )
            tally->covered++;

    for( int feat = 0; feat < feature_count; feat++ )
    {
        int const begin = feature_face_offset[feat];
        int const end = feature_face_offset[feat + 1];

        r->stamp++;
        r->touched_count = 0;
        for( int i = begin; i < end; i++ )
        {
            int const f = feature_faces[i];
            TRI_FOR_EACH_PIXEL(r, f, {
                if( r->scratch_stamp[at] != r->stamp )
                {
                    r->scratch_stamp[at] = r->stamp;
                    r->scratch_z[at] = z;
                    r->touched[r->touched_count++] = at;
                }
                else if( z < r->scratch_z[at] )
                    r->scratch_z[at] = z;
            });
        }

        for( int i = 0; i < r->touched_count; i++ )
        {
            int const at = r->touched[i];
            int const winner = r->near_feature[at];

            if( winner < 0 || winner == feat )
                continue;
            if( r->scratch_z[at] <= r->near_z[at] + slack )
                continue; /* coplanar with the winner: no order can be wrong */

            if( r->paint_feature[at] == feat )
                fixable[(long)winner * feature_count + feat]++;
            else
                breakable[(long)feat * feature_count + winner]++;
        }
    }
}

/**
 * Score one band assignment against the z-buffer, over every pose and angle.
 *
 * This is the objective that matters and the one to rank by: the count of
 * pixels where the painter's sort leaves a surface behind the one a depth
 * buffer would have shown. It is measured end to end -- real draw order, real
 * raster coverage -- rather than predicted, so it cannot be fooled by the
 * pairwise model being a first-order approximation.
 *
 * Also fills fixable/breakable when asked, because the solver needs those and
 * they fall out of the same traversal; pass NULL to score only.
 */
static struct tally
evaluate_assignment(
    struct geometry* g,
    struct raster* r,
    struct poses* poses,
    const int* band,
    int feature_count,
    const int* feature_face_offset,
    const int* feature_faces,
    long* fixable,
    long* breakable,
    int pose_count,
    int yaw_steps,
    int pitch_steps,
    double elev_min_deg,
    double elev_max_deg,
    int res,
    int scale,
    int distance)
{
    struct tally tally = { 0, 0, 0 };
    long* fx = fixable;
    long* bk = breakable;
    static long* scratch_fx = NULL;
    static long* scratch_bk = NULL;
    static int scratch_n = 0;

    /* The traversal always writes somewhere; a score-only call gets scratch
     * rather than a branch in the inner loop. */
    if( !fx || !bk )
    {
        if( scratch_n < feature_count )
        {
            free(scratch_fx);
            free(scratch_bk);
            scratch_fx = (long*)calloc((size_t)feature_count * (size_t)feature_count, sizeof(long));
            scratch_bk = (long*)calloc((size_t)feature_count * (size_t)feature_count, sizeof(long));
            scratch_n = (scratch_fx && scratch_bk) ? feature_count : 0;
        }
        fx = scratch_fx;
        bk = scratch_bk;
        if( !fx || !bk )
            return tally;
    }
    memset(fx, 0, (size_t)feature_count * (size_t)feature_count * sizeof(long));
    memset(bk, 0, (size_t)feature_count * (size_t)feature_count * sizeof(long));

    for( int pose = 0; pose < pose_count; pose++ )
    {
        int pose_distance = distance;
        if( poses->anim && !apply_pose(poses, g, pose, res, scale, &pose_distance) )
        {
            poses->rejected++;
            continue;
        }
        for( int p = 0; p < pitch_steps; p++ )
        {
            /* Only the elevations the client can actually produce. app.c clamps
             * world camera pitch to 128..383 of 2048; scoring from underneath
             * would rank assignments on breakage no player can see. */
            double pitch =
                elev_min_deg * PRIO_PI / 180.0 +
                (elev_max_deg - elev_min_deg) * PRIO_PI / 180.0 *
                    (pitch_steps == 1 ? 0.5 : (double)p / (pitch_steps - 1));
            for( int y = 0; y < yaw_steps; y++ )
            {
                struct view v;
                view_make(&v, 2.0 * PRIO_PI * y / yaw_steps, pitch);
                project_view(g, &v, r, pose_distance, scale);
                measure_view(
                    g, r, feature_count, feature_face_offset, feature_faces, band, fx, bk,
                    &tally, 2);
            }
        }
    }
    return tally;
}

/**
 * One complete band assignment, and what it actually scored.
 *
 * A candidate is a proposal, not a decision. Proposals come from the pairwise
 * model at various evidence floors; the decision comes from
 * evaluate_assignment, which renders and counts. Keeping the two apart is the
 * whole design: the pairwise net is a first-order approximation that is wrong
 * about three-way interactions, and letting it both propose and judge is how a
 * mapping that looks good on paper ships.
 */
struct assignment
{
    int* band;
    char name[48];
    long wrong;      /* pixels behind the z-buffer, the ranking key */
    long covered;
    int bands_used;
    bool bulk_flex;
};

/* --------------------------------------------------------- band solving -- */

/** Pixels won, minus pixels lost, by the current band assignment. */
static long
objective(int feature_count, const long* net, const int* band)
{
    long total = 0;
    for( int a = 0; a < feature_count; a++ )
        for( int b = 0; b < feature_count; b++ )
            if( band[a] > band[b] )
                total += net[(long)a * feature_count + b];
    return total;
}

/**
 * Hill climb the band assignment.
 *
 * Every feature is offered every band in turn and keeps the one with the best
 * objective, repeating until a whole sweep changes nothing. Starting from all
 * zero -- the pure depth sort -- means the objective only ever rises, so the
 * result is never worse than shipping no priorities at all, which is the one
 * guarantee worth having when the alternative is a spike through the head.
 *
 * The move is evaluated in O(features) by touching only the pairs the moved
 * feature is part of; the rest of the sum is unchanged by definition.
 */
static long
solve_bands(int feature_count, const long* net, int* band, int max_sweeps)
{
    long total = objective(feature_count, net, band);

    for( int sweep = 0; sweep < max_sweeps; sweep++ )
    {
        bool moved = false;

        for( int x = 0; x < feature_count; x++ )
        {
            int const from = band[x];
            int best_band = from;
            long best_delta = 0;

            for( int to = 0; to < HARD_BANDS; to++ )
            {
                long delta = 0;
                if( to == from )
                    continue;
                for( int b = 0; b < feature_count; b++ )
                {
                    if( b == x )
                        continue;
                    /* x above b */
                    delta += net[(long)x * feature_count + b] *
                             ((to > band[b]) - (from > band[b]));
                    /* b above x */
                    delta += net[(long)b * feature_count + x] *
                             ((band[b] > to) - (band[b] > from));
                }
                if( delta > best_delta )
                {
                    best_delta = delta;
                    best_band = to;
                }
            }

            if( best_band != from )
            {
                band[x] = best_band;
                total += best_delta;
                moved = true;
            }
        }
        if( !moved )
            break;
    }
    return total;
}

/**
 * Slide the used bands down onto 0,1,2,... .
 *
 * The climb leaves gaps, and a gap is not free: the flexible-priority splice
 * points are averages over bands 1/2, 3/4 and 6/8, so which numbers are
 * occupied changes the order even when their relative sequence does not.
 * Occupying a contiguous run from 0 keeps the assignment meaning exactly what
 * it was solved to mean.
 */
static void
compact_bands(int feature_count, int* band)
{
    int map[HARD_BANDS];
    int next = 0;

    for( int b = 0; b < HARD_BANDS; b++ )
        map[b] = -1;
    for( int i = 0; i < feature_count; i++ )
        map[band[i]] = 1;
    for( int b = 0; b < HARD_BANDS; b++ )
        if( map[b] == 1 )
            map[b] = next++;
    for( int i = 0; i < feature_count; i++ )
        band[i] = map[band[i]];
}


/* ------------------------------------------------------- the slow search -- */

/* Deterministic PRNG, so a run can be replayed from its recorded seed. */
static uint32_t
xorshift32(uint32_t* state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

/** Allocate the projection/paint scratch one annealing thread owns. */
static bool
raster_scratch_alloc(struct raster* r, int res, int vertex_count, int face_count)
{
    memset(r, 0, sizeof(*r));
    r->w = r->h = res;
    r->paint_feature = (int*)malloc((size_t)res * res * sizeof(int));
    r->paint_z = (int*)malloc((size_t)res * res * sizeof(int));
    r->sx = (int*)malloc((size_t)vertex_count * sizeof(int));
    r->sy = (int*)malloc((size_t)vertex_count * sizeof(int));
    r->sz = (int*)malloc((size_t)vertex_count * sizeof(int));
    r->face_depth = (int*)malloc((size_t)face_count * sizeof(int));
    r->face_band = (int*)malloc((size_t)face_count * sizeof(int));
    r->face_order = (int*)malloc((size_t)face_count * sizeof(int));
    return r->paint_feature && r->paint_z && r->sx && r->sy && r->sz && r->face_depth &&
           r->face_band && r->face_order;
}

static void
raster_scratch_free(struct raster* r)
{
    free(r->paint_feature);
    free(r->paint_z);
    free(r->sx);
    free(r->sy);
    free(r->sz);
    free(r->face_depth);
    free(r->face_band);
    free(r->face_order);
    memset(r, 0, sizeof(*r));
}

/** The z-buffer's answer for one view: which feature owns each pixel, how deep. */
static void
raster_zbuffer_ref(struct geometry* g, struct raster* r, int* ref_id, int* ref_z)
{
    for( int i = 0; i < r->w * r->h; i++ )
    {
        ref_id[i] = -1;
        ref_z[i] = 1 << 30;
    }
    for( int f = 0; f < g->face_count; f++ )
    {
        int const feat = g->face_feature[f];
        TRI_FOR_EACH_PIXEL(r, f, {
            if( z < ref_z[at] )
            {
                ref_z[at] = z;
                ref_id[at] = feat;
            }
        });
    }
}

/** The painter's answer for one view under a band assignment. */
static void
raster_painter(struct geometry* g, struct raster* r, const int* band)
{
    for( int i = 0; i < r->w * r->h; i++ )
    {
        r->paint_feature[i] = -1;
        r->paint_z[i] = 1 << 30;
    }
    for( int f = 0; f < g->face_count; f++ )
    {
        r->face_depth[f] = (r->sz[g->fa[f]] + r->sz[g->fb[f]] + r->sz[g->fc[f]]) / 3;
        r->face_band[f] = band[g->face_feature[f]];
        r->face_order[f] = f;
    }
    g_sort_depth = r->face_depth;
    g_sort_band = r->face_band;
    qsort(r->face_order, (size_t)g->face_count, sizeof(int), cmp_face_order);
    for( int i = 0; i < g->face_count; i++ )
    {
        int const f = r->face_order[i];
        int const feat = g->face_feature[f];
        TRI_FOR_EACH_PIXEL(r, f, {
            r->paint_feature[at] = feat;
            r->paint_z[at] = z;
        });
    }
}

struct slowview
{
    struct view v;
    int distance;
    int* ref_id; /* z-buffer of the PRISTINE geometry: the target */
    int* ref_z;
};

struct slowctx
{
    struct slowview* views;
    int view_count;
    struct raster* rasters; /* one per thread */
    int thread_count;
    int res;
    int scale;
};

/**
 * Pixels where the candidate's painter render leaves a surface visibly behind
 * the surface the ORIGINAL model's z-buffer shows. This is the whole objective:
 * the perturbed geometry is judged against the pristine reference, so vertex
 * fuzz cannot cheat by moving the goalposts along with the model.
 */
static long
slow_eval(struct geometry* g2, const int* band, struct slowctx* ctx)
{
    long total = 0;
    int const slack = 6; /* offsets reach 6 units; a shift below that is invisible */

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) reduction(+ : total) \
    num_threads(ctx->thread_count)
#endif
    for( int vi = 0; vi < ctx->view_count; vi++ )
    {
#ifdef _OPENMP
        struct raster* r = &ctx->rasters[omp_get_thread_num()];
#else
        struct raster* r = &ctx->rasters[0];
#endif
        struct slowview* sv = &ctx->views[vi];
        long wrong = 0;

        project_view(g2, &sv->v, r, sv->distance, ctx->scale);
        raster_painter(g2, r, band);
        for( int i = 0; i < r->w * r->h; i++ )
        {
            if( r->paint_feature[i] < 0 || sv->ref_id[i] < 0 )
                continue;
            if( r->paint_feature[i] != sv->ref_id[i] &&
                r->paint_z[i] > sv->ref_z[i] + slack )
                wrong++;
        }
        total += wrong;
    }
    return total;
}

/* ------------------------------------------------------------------ main -- */

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage: %s --in FILE.ob3 [--in FILE.ob3 ...] [--out FILE.ob3 ...]\n"
        "       [--report] [--views N] [--pitches N] [--res N] [--rounds N]\n"
        "       [--min-pixels N] [--no-weld] [--elev MIN,MAX] [--bulk-flex]\n"
        "\n"
        "  --views N       yaw samples around the model (default 12)\n"
        "  --pitches N     elevation samples across --elev (default 5)\n"
        "  --elev MIN,MAX  elevation range in degrees, default -20,67. The max\n"
        "                  is the client's own camera pitch clamp (app.c,\n"
        "                  128..383 of 2048); the min is how far perspective\n"
        "                  lifts a model taller than the camera. Widening it\n"
        "                  past what the client can produce scores real\n"
        "                  orderings away as conflicts.\n"
        "  --res N         analysis raster size (default 224)\n"
        "  --min-pixels N  ignore a pair thinner than this across all views\n"
        "  --no-weld       segment on shared vertex indices only, without\n"
        "                  welding coincident positions first\n"
        "  --bulk-flex     put the unpromoted bulk at priority 10 (flexible)\n"
        "                  rather than band 0, so a promoted feature splices in\n"
        "                  at its own depth instead of pinning in front\n"
        "\n"
        "Analysis runs over every --in together; each --out receives the slice\n"
        "belonging to the --in at the same position. Inputs are never written.\n",
        argv0);
}

int
main(int argc, char** argv)
{
    struct input inputs[MAX_INPUTS];
    int input_count = 0;
    int out_count = 0;
    bool do_report = false;
    bool weld = true;
    int yaw_steps = 12;
    int pitch_steps = 5;
    int res = 224;
    double elev_min_deg = -20.0;
    double elev_max_deg = 67.0;
    const char* anim_path = NULL;
    const char* framemap_path = NULL;
    const char* cache_dir = NULL;
    const char* frames_arg = NULL;
    int frame_stride = 1;
    /* The lane's animation assets are pass-through: the importer copied the
     * bytes rather than re-encoding them, so the codec on disk is the SOURCE
     * revision's, not the destination's. Defaults are what the RS727 source
     * used; the client's own osrs239 default (frame V1) is reachable with
     * --frame-codec 1 for comparison. */
    int frame_codec = RSCACHE_CODEC_FRAME_V2;
    int framemap_codec = RSCACHE_CODEC_FRAMEMAP_V3;
    struct poses poses = { NULL, NULL, NULL, 0, 0, 0 };
    int pose_count = 1;

    struct geometry g = { 0 };
    struct feature* features = NULL;
    struct raster r = { 0 };
    long* fixable = NULL;
    long* breakable = NULL;
    long* net = NULL;
    int* band = NULL;
    int* previous = NULL;
    struct tally tally = { 0, 0, 0 };
    struct tally baseline = { 0, 0, 0 };
    struct assignment* proposals = NULL;
    int slow_iters = 0;
    int slow_res = 128;
    int slow_yaws = 6;
    int slow_pitches = 3;
    int slow_max_offset = 6;
    uint32_t slow_seed = 0x51F0D5u;
    int* slow_delta_x = NULL; /* per-vertex, g-space; written into the ob3 */
    int* slow_delta_y = NULL;
    int* slow_delta_z = NULL;
    struct slowctx sctx = { 0 };
    int candidate_count = 0;
    long gain = 0;
    int feature_count = 0;
    int* feature_face_offset = NULL;
    int* feature_faces = NULL;
    int distance = 0, scale = 0, extent = 0;
    int rc = 1;

    memset(inputs, 0, sizeof(inputs));

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--in") == 0 && i + 1 < argc )
        {
            if( input_count >= MAX_INPUTS )
            {
                fprintf(stderr, "rs2012_face_priorities: too many --in\n");
                return 2;
            }
            inputs[input_count++].in_path = argv[++i];
        }
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
        {
            if( out_count >= MAX_INPUTS )
            {
                fprintf(stderr, "rs2012_face_priorities: too many --out\n");
                return 2;
            }
            inputs[out_count++].out_path = argv[++i];
        }
        else if( strcmp(argv[i], "--report") == 0 )
            do_report = true;
        else if( strcmp(argv[i], "--no-weld") == 0 )
            weld = false;
        else if( strcmp(argv[i], "--slow") == 0 && i + 1 < argc )
            slow_iters = atoi(argv[++i]);
        else if( strcmp(argv[i], "--slow-res") == 0 && i + 1 < argc )
            slow_res = atoi(argv[++i]);
        else if( strcmp(argv[i], "--slow-views") == 0 && i + 1 < argc )
            slow_yaws = atoi(argv[++i]);
        else if( strcmp(argv[i], "--slow-pitches") == 0 && i + 1 < argc )
            slow_pitches = atoi(argv[++i]);
        else if( strcmp(argv[i], "--slow-max-offset") == 0 && i + 1 < argc )
            slow_max_offset = atoi(argv[++i]);
        else if( strcmp(argv[i], "--slow-seed") == 0 && i + 1 < argc )
            slow_seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            cache_dir = argv[++i];
        else if( strcmp(argv[i], "--anim") == 0 && i + 1 < argc )
            anim_path = argv[++i];
        else if( strcmp(argv[i], "--framemap") == 0 && i + 1 < argc )
            framemap_path = argv[++i];
        else if( strcmp(argv[i], "--frames") == 0 && i + 1 < argc )
            frames_arg = argv[++i];
        else if( strcmp(argv[i], "--frame-stride") == 0 && i + 1 < argc )
            frame_stride = atoi(argv[++i]);
        else if( strcmp(argv[i], "--frame-codec") == 0 && i + 1 < argc )
            frame_codec = atoi(argv[++i]);
        else if( strcmp(argv[i], "--framemap-codec") == 0 && i + 1 < argc )
            framemap_codec = atoi(argv[++i]);
        else if( strcmp(argv[i], "--views") == 0 && i + 1 < argc )
            yaw_steps = atoi(argv[++i]);
        else if( strcmp(argv[i], "--pitches") == 0 && i + 1 < argc )
            pitch_steps = atoi(argv[++i]);
        else if( strcmp(argv[i], "--res") == 0 && i + 1 < argc )
            res = atoi(argv[++i]);
        else if( strcmp(argv[i], "--elev") == 0 && i + 1 < argc )
        {
            if( sscanf(argv[++i], "%lf,%lf", &elev_min_deg, &elev_max_deg) != 2 )
            {
                usage(argv[0]);
                return 2;
            }
        }
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if( input_count == 0 || (out_count && out_count != input_count) || yaw_steps < 1 ||
        pitch_steps < 1 || res < 32 || res > 1024 )
    {
        usage(argv[0]);
        return 2;
    }

    /* ---- load and concatenate ---- */
    for( int i = 0; i < input_count; i++ )
    {
        long size = 0;
        uint8_t* bytes = read_file(inputs[i].in_path, &size);
        if( !bytes )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot read %s\n", inputs[i].in_path);
            goto done;
        }
        inputs[i].model =
            RSCache_ModelNewDecodeProvenance(bytes, (int)size, &inputs[i].provenance);
        free(bytes);
        if( !inputs[i].model || !inputs[i].provenance )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot decode %s\n", inputs[i].in_path);
            goto done;
        }
        inputs[i].vertex_base = g.vertex_count;
        inputs[i].face_base = g.face_count;
        g.vertex_count += inputs[i].model->vertex_count;
        g.face_count += inputs[i].model->face_count;
    }

    g.vx = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    g.vy = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    g.vz = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    g.fa = (int*)malloc((size_t)g.face_count * sizeof(int));
    g.fb = (int*)malloc((size_t)g.face_count * sizeof(int));
    g.fc = (int*)malloc((size_t)g.face_count * sizeof(int));
    g.face_feature = (int*)malloc((size_t)g.face_count * sizeof(int));
    if( !g.vx || !g.vy || !g.vz || !g.fa || !g.fb || !g.fc || !g.face_feature )
        goto done;

    for( int i = 0; i < input_count; i++ )
    {
        const struct RSCache_Model* m = inputs[i].model;
        /* Version-13+ ob3s store vertices at 4x; the engine's adaptor shifts
         * them down on the way in, and this tool has to agree with it or two
         * inputs at different versions land at different scales. */
        int const shift = m->format_version >= 13 ? 2 : 0;
        for( int v = 0; v < m->vertex_count; v++ )
        {
            g.vx[inputs[i].vertex_base + v] = m->vertices_x[v] >> shift;
            g.vy[inputs[i].vertex_base + v] = m->vertices_y[v] >> shift;
            g.vz[inputs[i].vertex_base + v] = m->vertices_z[v] >> shift;
        }
        for( int f = 0; f < m->face_count; f++ )
        {
            g.fa[inputs[i].face_base + f] = m->face_indices_a[f] + inputs[i].vertex_base;
            g.fb[inputs[i].face_base + f] = m->face_indices_b[f] + inputs[i].vertex_base;
            g.fc[inputs[i].face_base + f] = m->face_indices_c[f] + inputs[i].vertex_base;
        }
    }

    /* ---- poses ----
     *
     * Built before segmentation so the bind pose is what features are cut
     * from: connectivity is a property of the mesh, not of a pose, and cutting
     * it per frame would give a different feature set every frame. */
    if( cache_dir || anim_path || framemap_path || frames_arg )
    {
        struct ToriDraw_Model* parts[MAX_INPUTS] = { NULL };
        int selected = 0;

        if( !frames_arg || (!cache_dir && (!anim_path || !framemap_path)) )
        {
            fprintf(
                stderr,
                "rs2012_face_priorities: --frames needs either --cache, or "
                "--anim with --framemap\n");
            goto done;
        }
        if( frame_stride < 1 )
            frame_stride = 1;

        /* The frame list is the sequence's, in sequence order, so a stride
         * samples the whole arc evenly rather than one end of it. */
        {
            const char* s = frames_arg;
            int capacity = 1;
            for( const char* c = frames_arg; *c; c++ )
                if( *c == ',' )
                    capacity++;
            poses.frames = (int*)malloc((size_t)capacity * sizeof(int));
            if( !poses.frames )
                goto done;
            for( int n = 0; *s; n++ )
            {
                int const value = (int)strtol(s, (char**)&s, 10);
                if( n % frame_stride == 0 )
                    poses.frames[selected++] = value;
                if( *s == ',' )
                    s++;
            }
        }
        poses.frame_count = selected;

        /* The client merges an npc's models and animates the merge, so the
         * bones and the vertex order have to come from the same merge. The
         * concatenation above walks the inputs in the same order, which is what
         * lets one vertex index address both. */
        for( int i = 0; i < input_count; i++ )
        {
            /* Decoded a second time on purpose: ToriRS_ModelFromRSCache MOVES
             * the arrays out of its source, and inputs[i].model still has to be
             * encodable at the end. A throwaway decode is cheaper than a deep
             * copy of a struct this wide, and cannot drift from it. */
            long size = 0;
            uint8_t* bytes = read_file(inputs[i].in_path, &size);
            struct RSCache_Model* copy =
                bytes ? RSCache_ModelNewDecode(bytes, (int)size) : NULL;
            struct ToriRS_Model* mid = copy ? ToriRS_ModelFromRSCache(copy) : NULL;

            free(bytes);
            RSCache_ModelFree(copy);
            parts[i] = mid ? ToriDraw_ModelFromToriRS(mid) : NULL;
            ToriRS_ModelFree(mid);
            if( !parts[i] )
            {
                fprintf(stderr, "rs2012_face_priorities: cannot build an animatable model\n");
                for( int k = 0; k < input_count; k++ )
                    ToriDraw_ModelFree(parts[k]);
                goto done;
            }
        }
        poses.model = ToriDraw_ModelNewMerge(parts, input_count);
        for( int i = 0; i < input_count; i++ )
            ToriDraw_ModelFree(parts[i]);
        if( !poses.model || poses.model->vertex_count != g.vertex_count )
        {
            fprintf(
                stderr,
                "rs2012_face_priorities: merged model is %d vertices, geometry is %d\n",
                poses.model ? poses.model->vertex_count : -1,
                g.vertex_count);
            goto done;
        }
        ToriDraw_ModelCaptureOriginalVertices(poses.model);

        poses.anim = cache_dir
                         ? load_animation_from_cache(cache_dir, poses.frames, poses.frame_count)
                         : load_animation(
                               anim_path, framemap_path, poses.frames, poses.frame_count,
                               frame_codec, framemap_codec);
        if( !poses.anim )
            goto done;
        pose_count = 1 + poses.anim->frame_count;
        printf(
            "poses:    bind pose + %d of %s (stride %d)\n",
            poses.anim->frame_count,
            cache_dir ? cache_dir : anim_path,
            frame_stride);
    }

    feature_count = segment(&g, weld);
    if( feature_count <= 0 || feature_count > MAX_FEATURES )
    {
        fprintf(
            stderr,
            "rs2012_face_priorities: %d features, outside the %d the tool holds\n",
            feature_count,
            MAX_FEATURES);
        goto done;
    }

    features = (struct feature*)calloc((size_t)feature_count, sizeof(struct feature));
    feature_face_offset = (int*)calloc((size_t)feature_count + 1, sizeof(int));
    feature_faces = (int*)malloc((size_t)g.face_count * sizeof(int));
    fixable = (long*)calloc((size_t)feature_count * (size_t)feature_count, sizeof(long));
    breakable = (long*)calloc((size_t)feature_count * (size_t)feature_count, sizeof(long));
    if( !features || !feature_face_offset || !feature_faces || !fixable || !breakable )
        goto done;

    for( int f = 0; f < g.face_count; f++ )
        features[g.face_feature[f]].face_count++;
    for( int i = 0; i < feature_count; i++ )
        feature_face_offset[i + 1] = feature_face_offset[i] + features[i].face_count;
    {
        int* cursor = (int*)malloc((size_t)feature_count * sizeof(int));
        if( !cursor )
            goto done;
        memcpy(cursor, feature_face_offset, (size_t)feature_count * sizeof(int));
        for( int f = 0; f < g.face_count; f++ )
            feature_faces[cursor[g.face_feature[f]]++] = f;
        free(cursor);
    }

    /* ---- framing ---- */
    {
        int min_v[3] = { 1 << 30, 1 << 30, 1 << 30 };
        int max_v[3] = { -(1 << 30), -(1 << 30), -(1 << 30) };
        int center[3];
        long radius2 = 0;

        for( int v = 0; v < g.vertex_count; v++ )
        {
            int c[3] = { g.vx[v], g.vy[v], g.vz[v] };
            for( int k = 0; k < 3; k++ )
            {
                if( c[k] < min_v[k] ) min_v[k] = c[k];
                if( c[k] > max_v[k] ) max_v[k] = c[k];
            }
        }
        for( int k = 0; k < 3; k++ )
            center[k] = (min_v[k] + max_v[k]) / 2;
        for( int v = 0; v < g.vertex_count; v++ )
        {
            long dx = g.vx[v] -= center[0];
            long dy = g.vy[v] -= center[1];
            long dz = g.vz[v] -= center[2];
            long d2 = dx * dx + dy * dy + dz * dz;
            if( d2 > radius2 )
                radius2 = d2;
        }
        extent = (int)sqrt((double)radius2) + 1;
        scale = 512;
        /* Far enough that the whole model fits whatever way it turns. */
        distance = (int)((double)extent * scale / (0.45 * res)) + extent;
    }

    /* ---- measure ---- */
    r.w = r.h = res;
    r.near_feature = (int*)malloc((size_t)res * res * sizeof(int));
    r.near_z = (int*)malloc((size_t)res * res * sizeof(int));
    r.paint_feature = (int*)malloc((size_t)res * res * sizeof(int));
    r.paint_z = (int*)malloc((size_t)res * res * sizeof(int));
    r.scratch_z = (int*)malloc((size_t)res * res * sizeof(int));
    r.scratch_stamp = (int*)calloc((size_t)res * res, sizeof(int));
    r.touched = (int*)malloc((size_t)res * res * sizeof(int));
    r.sx = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    r.sy = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    r.sz = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    r.face_depth = (int*)malloc((size_t)g.face_count * sizeof(int));
    r.face_band = (int*)malloc((size_t)g.face_count * sizeof(int));
    r.face_order = (int*)malloc((size_t)g.face_count * sizeof(int));
    if( !r.near_feature || !r.near_z || !r.paint_feature || !r.paint_z || !r.scratch_z ||
        !r.scratch_stamp ||
        !r.touched || !r.sx || !r.sy || !r.sz || !r.face_depth || !r.face_band || !r.face_order )
        goto done;

    band = (int*)malloc((size_t)feature_count * sizeof(int));
    net = (long*)malloc((size_t)feature_count * (size_t)feature_count * sizeof(long));
    previous = (int*)malloc((size_t)feature_count * sizeof(int));
    if( !band || !net || !previous )
        goto done;

    /* Start in the middle, not at zero.
     *
     * The climb only ever moves one feature at a time, so from band 0 the only
     * available move is "put this one over everything". The relation that
     * actually wants expressing is usually the opposite -- a single feature
     * needs to go BEHIND the rest, and reaching that from zero would take a
     * coordinated move of every other feature at once, which single-feature
     * hill climbing cannot find. From the middle both directions are one move
     * away. compact_bands() slides the answer back down afterwards, so the
     * starting band is scaffolding and not part of the result. */
    for( int i = 0; i < feature_count; i++ )
        band[i] = HARD_BANDS / 2;

    /* ---- propose, score, rank, keep the best ----
     *
     * There is no enumerating this space: twelve bands over N features is
     * 12^N, and N is 216 on the QBD. So candidates come from a handful of
     * strategies and the ranking is done by the honest objective.
     *
     * "Depth sort, no bands" is always a candidate, and it is exactly the
     * stripped model. If nothing beats it, that is the finding and the tool
     * reports it, rather than shipping a worse model with a confident table. */
    {
        long const floors[] = { 10, 40, 150, 400 };
        int const floor_count = (int)(sizeof(floors) / sizeof(floors[0]));
        /* No bulk-flex candidates: this evaluator's painter sorts strictly
         * by (band, depth), while the real sorter SPLICES priorities 10/11
         * into the fixed run at three depth averages. Ranking a flex
         * spelling with the wrong rule scored it at 40%% wrong for reasons
         * that were the model's, not the content's. */
        int const slots = floor_count + 2;
        int best = 0;

        proposals = (struct assignment*)calloc((size_t)slots, sizeof(struct assignment));
        if( !proposals )
            goto done;
        for( int c = 0; c < slots; c++ )
        {
            proposals[c].band = (int*)malloc((size_t)feature_count * sizeof(int));
            if( !proposals[c].band )
                goto done;
        }

        for( int i = 0; i < feature_count; i++ )
            proposals[0].band[i] = 0;
        snprintf(proposals[0].name, sizeof(proposals[0].name), "depth sort (no bands)");
        candidate_count = 1;

        /* What the input already carries, so a lane re-packed with inherited
         * priorities is ranked against its own shipping state instead of
         * against an assumption about it. */
        {
            bool any = false;
            for( int i = 0; i < feature_count; i++ )
                proposals[1].band[i] = 0;
            for( int i = 0; i < input_count; i++ )
            {
                const struct RSCache_Model* m = inputs[i].model;
                if( !m->face_priorities )
                    continue;
                any = true;
                for( int f = 0; f < m->face_count; f++ )
                {
                    int const feat = g.face_feature[inputs[i].face_base + f];
                    int const prio = m->face_priorities[f];
                    proposals[1].band[feat] = prio < HARD_BANDS ? prio : HARD_BANDS - 1;
                }
            }
            if( any )
            {
                snprintf(
                    proposals[1].name, sizeof(proposals[1].name), "as shipped (inherited)");
                candidate_count = 2;
            }
        }

        /* The hill climb at several evidence floors, each also in a
         * bulk-flexible spelling. Too low a floor chases noise and too high
         * sees nothing; which is which is a property of the model, so try a
         * spread and let the ranking decide instead of hardcoding one. */
        for( int fi = 0; fi < floor_count; fi++ )
        {
            int* work = proposals[candidate_count].band;

            for( int i = 0; i < feature_count; i++ )
                work[i] = HARD_BANDS / 2;
            evaluate_assignment(
                &g, &r, &poses, work, feature_count, feature_face_offset, feature_faces,
                fixable, breakable, pose_count, yaw_steps, pitch_steps, elev_min_deg,
                elev_max_deg, res, scale, distance);

            for( int a = 0; a < feature_count; a++ )
                for( int b = 0; b < feature_count; b++ )
                {
                    long const fix = fixable[(long)a * feature_count + b];
                    long const brk = breakable[(long)a * feature_count + b];
                    long value = 0;
                    if( a != b && (fix >= floors[fi] || brk >= floors[fi]) && fix - brk > 0 )
                        value = fix - brk;
                    net[(long)a * feature_count + b] = value;
                }
            solve_bands(feature_count, net, work, 32);
            compact_bands(feature_count, work);
            snprintf(
                proposals[candidate_count].name,
                sizeof(proposals[candidate_count].name),
                "climb, floor %ld",
                floors[fi]);
            candidate_count++;

        }

        /* ---- score every proposal the same way ---- */
        for( int c = 0; c < candidate_count; c++ )
        {
            struct tally t;
            unsigned char seen[HARD_BANDS + 2];

            t = evaluate_assignment(
                &g, &r, &poses, proposals[c].band, feature_count, feature_face_offset,
                feature_faces, NULL, NULL, pose_count, yaw_steps, pitch_steps,
                elev_min_deg, elev_max_deg, res, scale, distance);

            memset(seen, 0, sizeof(seen));
            proposals[c].bands_used = 0;
            for( int i = 0; i < feature_count; i++ )
            {
                int const b = proposals[c].band[i];
                if( b >= 0 && b <= HARD_BANDS + 1 && !seen[b] )
                {
                    seen[b] = 1;
                    proposals[c].bands_used++;
                }
            }
            proposals[c].wrong = t.wrong_within + t.wrong_between;
            proposals[c].covered = t.covered;
            if( proposals[c].wrong < proposals[best].wrong )
                best = c;
        }

        /* ---- the table, in rank order: it is the argument ---- */
        printf("\ncandidates, ranked by pixels left behind the z-buffer:\n");
        printf("%-30s %10s %9s %6s\n", "strategy", "wrong", "of drawn", "bands");
        {
            unsigned char shown[32] = { 0 };
            for( int n = 0; n < candidate_count && n < 32; n++ )
            {
                int pick = -1;
                for( int c = 0; c < candidate_count; c++ )
                    if( !shown[c] && (pick < 0 || proposals[c].wrong < proposals[pick].wrong) )
                        pick = c;
                if( pick < 0 )
                    break;
                shown[pick] = 1;
                printf(
                    "%-30s %10ld %8.3f%% %6d%s\n",
                    proposals[pick].name,
                    proposals[pick].wrong,
                    proposals[pick].covered
                        ? 100.0 * (double)proposals[pick].wrong /
                              (double)proposals[pick].covered
                        : 0.0,
                    proposals[pick].bands_used,
                    pick == best ? "   <- chosen" : "");
            }
        }

        memcpy(band, proposals[best].band, (size_t)feature_count * sizeof(int));
        baseline.wrong_between = proposals[0].wrong;
        baseline.covered = proposals[0].covered;
        tally.wrong_between = proposals[best].wrong;
        tally.covered = proposals[best].covered;
        gain = proposals[0].wrong - proposals[best].wrong;

        if( best == 0 )
            printf(
                "\nno band assignment beat the plain depth sort; writing it unbanded.\n"
                "That is the result, not a failure: this model's remaining error is not\n"
                "reachable by priorities.\n");
    }


    /* ---- the slow search: anneal bands AND geometry against the z-buffer ----
     *
     * The fast rank can only choose an order for the geometry it was given.
     * Some error is not reachable that way at all: two surfaces interleaving at
     * near-equal depth have no right order, only a right geometry. So the slow
     * search widens the state to (band per feature, radial offset per feature)
     * -- each feature may move up to a few units toward or away from its own
     * centroid, a real, encodable edit far below visual threshold on a model
     * ~1,700 units deep -- and anneals both together.
     *
     * The judge does not move: candidates are scored against the z-buffer of
     * the PRISTINE geometry, so the fuzz cannot improve its score by dragging
     * the reference along with the mistake. CPU-parallel over views (OpenMP).
     */
    if( slow_iters > 0 )
    {
        int const fc2 = feature_count;
        struct geometry g2 = g; /* shares faces; gets its own vertex arrays */
        int* vertex_feature = NULL;
        float* dir_x = NULL;
        float* dir_y = NULL;
        float* dir_z = NULL;
        int* off = NULL;
        int* best_off = NULL;
        int* work_band = NULL;
        int* best_band = NULL;
        long cur_cost, best_cost, base_cost;
        uint32_t rng = slow_seed;
        int accepted = 0, improved = 0;

        sctx.res = slow_res;
        sctx.scale = 512;
        sctx.view_count = slow_yaws * slow_pitches;
#ifdef _OPENMP
        sctx.thread_count = omp_get_max_threads();
        if( sctx.thread_count > 8 )
            sctx.thread_count = 8;
#else
        sctx.thread_count = 1;
#endif

        g2.vx = (int*)malloc((size_t)g.vertex_count * sizeof(int));
        g2.vy = (int*)malloc((size_t)g.vertex_count * sizeof(int));
        g2.vz = (int*)malloc((size_t)g.vertex_count * sizeof(int));
        vertex_feature = (int*)malloc((size_t)g.vertex_count * sizeof(int));
        dir_x = (float*)calloc((size_t)g.vertex_count, sizeof(float));
        dir_y = (float*)calloc((size_t)g.vertex_count, sizeof(float));
        dir_z = (float*)calloc((size_t)g.vertex_count, sizeof(float));
        off = (int*)calloc((size_t)fc2, sizeof(int));
        best_off = (int*)calloc((size_t)fc2, sizeof(int));
        work_band = (int*)malloc((size_t)fc2 * sizeof(int));
        best_band = (int*)malloc((size_t)fc2 * sizeof(int));
        sctx.views = (struct slowview*)calloc((size_t)sctx.view_count, sizeof(struct slowview));
        sctx.rasters = (struct raster*)calloc((size_t)sctx.thread_count, sizeof(struct raster));
        if( !g2.vx || !g2.vy || !g2.vz || !vertex_feature || !dir_x || !dir_y || !dir_z ||
            !off || !best_off || !work_band || !best_band || !sctx.views || !sctx.rasters )
            goto slow_done;

        memcpy(g2.vx, g.vx, (size_t)g.vertex_count * sizeof(int));
        memcpy(g2.vy, g.vy, (size_t)g.vertex_count * sizeof(int));
        memcpy(g2.vz, g.vz, (size_t)g.vertex_count * sizeof(int));

        /* Every vertex belongs to exactly one feature -- a shared vertex would
         * have merged the two components -- so the map is well defined. */
        for( int v = 0; v < g.vertex_count; v++ )
            vertex_feature[v] = -1;
        for( int f = 0; f < g.face_count; f++ )
        {
            vertex_feature[g.fa[f]] = g.face_feature[f];
            vertex_feature[g.fb[f]] = g.face_feature[f];
            vertex_feature[g.fc[f]] = g.face_feature[f];
        }
        {
            /* Radial directions from each feature's centroid: the offset
             * inflates or deflates a feature slightly, which is the edit that
             * separates two surfaces fighting at equal depth. */
            long* cx = (long*)calloc((size_t)fc2, sizeof(long));
            long* cy = (long*)calloc((size_t)fc2, sizeof(long));
            long* cz = (long*)calloc((size_t)fc2, sizeof(long));
            long* cn = (long*)calloc((size_t)fc2, sizeof(long));
            if( !cx || !cy || !cz || !cn )
            {
                free(cx); free(cy); free(cz); free(cn);
                goto slow_done;
            }
            for( int v = 0; v < g.vertex_count; v++ )
                if( vertex_feature[v] >= 0 )
                {
                    cx[vertex_feature[v]] += g.vx[v];
                    cy[vertex_feature[v]] += g.vy[v];
                    cz[vertex_feature[v]] += g.vz[v];
                    cn[vertex_feature[v]]++;
                }
            for( int v = 0; v < g.vertex_count; v++ )
            {
                int const F = vertex_feature[v];
                double dx, dy, dz, len;
                if( F < 0 || cn[F] == 0 )
                    continue;
                dx = g.vx[v] - (double)cx[F] / cn[F];
                dy = g.vy[v] - (double)cy[F] / cn[F];
                dz = g.vz[v] - (double)cz[F] / cn[F];
                len = sqrt(dx * dx + dy * dy + dz * dz);
                if( len > 1e-6 )
                {
                    dir_x[v] = (float)(dx / len);
                    dir_y[v] = (float)(dy / len);
                    dir_z[v] = (float)(dz / len);
                }
            }
            free(cx); free(cy); free(cz); free(cn);
        }

        /* Views and their pristine z-buffer references, computed once. */
        {
            struct raster ref_raster;
            if( !raster_scratch_alloc(&ref_raster, slow_res, g.vertex_count, g.face_count) )
                goto slow_done;
            for( int p = 0; p < slow_pitches; p++ )
            {
                double pitch =
                    elev_min_deg * PRIO_PI / 180.0 +
                    (elev_max_deg - elev_min_deg) * PRIO_PI / 180.0 *
                        (slow_pitches == 1 ? 0.5 : (double)p / (slow_pitches - 1));
                for( int y = 0; y < slow_yaws; y++ )
                {
                    struct slowview* sv = &sctx.views[p * slow_yaws + y];
                    view_make(&sv->v, 2.0 * PRIO_PI * y / slow_yaws, pitch);
                    sv->distance = distance;
                    sv->ref_id = (int*)malloc((size_t)slow_res * slow_res * sizeof(int));
                    sv->ref_z = (int*)malloc((size_t)slow_res * slow_res * sizeof(int));
                    if( !sv->ref_id || !sv->ref_z )
                    {
                        raster_scratch_free(&ref_raster);
                        goto slow_done;
                    }
                    project_view(&g, &sv->v, &ref_raster, sv->distance, sctx.scale);
                    raster_zbuffer_ref(&g, &ref_raster, sv->ref_id, sv->ref_z);
                }
            }
            raster_scratch_free(&ref_raster);
        }
        for( int t = 0; t < sctx.thread_count; t++ )
            if( !raster_scratch_alloc(&sctx.rasters[t], slow_res, g.vertex_count, g.face_count) )
                goto slow_done;

        /* Anneal, from the fast winner. */
        memcpy(work_band, band, (size_t)fc2 * sizeof(int));
        memcpy(best_band, band, (size_t)fc2 * sizeof(int));
        cur_cost = best_cost = base_cost = slow_eval(&g2, work_band, &sctx);
        printf(
            "\nslow search: %d iters, %d views @ %d px, %d thread(s), seed 0x%x\n"
            "slow start:  %ld wrong px (the fast winner under this objective)\n",
            slow_iters, sctx.view_count, slow_res, sctx.thread_count, slow_seed, base_cost);
        fflush(stdout);

        for( int it = 0; it < slow_iters; it++ )
        {
            double const t01 = (double)it / (slow_iters > 1 ? slow_iters - 1 : 1);
            /* Cold schedule on purpose: the search STARTS from the fast
             * winner, already a local optimum, so a hot walk only spends the
             * budget wandering above the incumbent -- measured on the QBD:
             * 4,000 iterations at 2%% never got back under its own start. */
            double const temp = 0.002 * (double)(base_cost > 0 ? base_cost : 1) *
                                pow(0.01, t01);
            int const F = (int)(xorshift32(&rng) % (uint32_t)fc2);
            int saved_band = work_band[F];
            int saved_off = off[F];
            long trial;
            bool geometry_moved = false;

            if( xorshift32(&rng) % 100 < 55 )
            {
                work_band[F] = (int)(xorshift32(&rng) % HARD_BANDS);
                if( work_band[F] == saved_band )
                    work_band[F] = (saved_band + 1) % HARD_BANDS;
            }
            else
            {
                int step = (xorshift32(&rng) & 2) ? 1 : -1;
                if( xorshift32(&rng) & 4 )
                    step *= 2;
                off[F] = off[F] + step;
                if( off[F] > slow_max_offset )
                    off[F] = slow_max_offset;
                if( off[F] < -slow_max_offset )
                    off[F] = -slow_max_offset;
                if( off[F] == saved_off )
                {
                    work_band[F] = saved_band;
                    continue;
                }
                geometry_moved = true;
                for( int v = 0; v < g.vertex_count; v++ )
                    if( vertex_feature[v] == F )
                    {
                        g2.vx[v] = g.vx[v] + (int)lround(dir_x[v] * off[F]);
                        g2.vy[v] = g.vy[v] + (int)lround(dir_y[v] * off[F]);
                        g2.vz[v] = g.vz[v] + (int)lround(dir_z[v] * off[F]);
                    }
            }

            trial = slow_eval(&g2, work_band, &sctx);
            if( trial <= cur_cost ||
                (temp > 0 &&
                 (double)(xorshift32(&rng) % 1000000) / 1000000.0 <
                     exp(-(double)(trial - cur_cost) / temp)) )
            {
                cur_cost = trial;
                accepted++;
                if( trial < best_cost )
                {
                    best_cost = trial;
                    improved++;
                    memcpy(best_band, work_band, (size_t)fc2 * sizeof(int));
                    memcpy(best_off, off, (size_t)fc2 * sizeof(int));
                }
            }
            else
            {
                work_band[F] = saved_band;
                if( geometry_moved )
                {
                    off[F] = saved_off;
                    for( int v = 0; v < g.vertex_count; v++ )
                        if( vertex_feature[v] == F )
                        {
                            g2.vx[v] = g.vx[v] + (int)lround(dir_x[v] * off[F]);
                            g2.vy[v] = g.vy[v] + (int)lround(dir_y[v] * off[F]);
                            g2.vz[v] = g.vz[v] + (int)lround(dir_z[v] * off[F]);
                        }
                }
            }
            /* Pull the walker home periodically; an anneal that ends far from
             * its best has been sightseeing, not searching. */
            if( (it + 1) % 500 == 0 && cur_cost > best_cost )
            {
                memcpy(work_band, best_band, (size_t)fc2 * sizeof(int));
                memcpy(off, best_off, (size_t)fc2 * sizeof(int));
                for( int v = 0; v < g.vertex_count; v++ )
                {
                    int const F = vertex_feature[v];
                    if( F >= 0 )
                    {
                        g2.vx[v] = g.vx[v] + (int)lround(dir_x[v] * off[F]);
                        g2.vy[v] = g.vy[v] + (int)lround(dir_y[v] * off[F]);
                        g2.vz[v] = g.vz[v] + (int)lround(dir_z[v] * off[F]);
                    }
                }
                cur_cost = best_cost;
            }
            if( (it + 1) % 200 == 0 )
            {
                printf(
                    "slow %5d/%d: current %ld, best %ld (%+.1f%%), accepted %d\n",
                    it + 1, slow_iters, cur_cost, best_cost,
                    base_cost ? 100.0 * (double)(best_cost - base_cost) / (double)base_cost
                              : 0.0,
                    accepted);
                fflush(stdout);
            }
        }

        printf(
            "slow result: %ld -> %ld wrong px (%+.1f%%), %d improvements, "
            "%d feature(s) moved\n",
            base_cost, best_cost,
            base_cost ? 100.0 * (double)(best_cost - base_cost) / (double)base_cost : 0.0,
            improved,
            ({
                int moved = 0;
                for( int F = 0; F < fc2; F++ )
                    if( best_off[F] )
                        moved++;
                moved;
            }));

        /* Adopt only a genuine improvement; the fuzz must never ship on a tie. */
        if( best_cost < base_cost )
        {
            memcpy(band, best_band, (size_t)fc2 * sizeof(int));
            slow_delta_x = (int*)calloc((size_t)g.vertex_count, sizeof(int));
            slow_delta_y = (int*)calloc((size_t)g.vertex_count, sizeof(int));
            slow_delta_z = (int*)calloc((size_t)g.vertex_count, sizeof(int));
            if( slow_delta_x && slow_delta_y && slow_delta_z )
                for( int v = 0; v < g.vertex_count; v++ )
                {
                    int const F = vertex_feature[v];
                    if( F >= 0 && best_off[F] )
                    {
                        slow_delta_x[v] = (int)lround(dir_x[v] * best_off[F]);
                        slow_delta_y[v] = (int)lround(dir_y[v] * best_off[F]);
                        slow_delta_z[v] = (int)lround(dir_z[v] * best_off[F]);
                    }
                }
        }
        else
            printf("slow search found nothing better; keeping the fast winner unchanged.\n");

    slow_done:
        for( int vi = 0; sctx.views && vi < sctx.view_count; vi++ )
        {
            free(sctx.views[vi].ref_id);
            free(sctx.views[vi].ref_z);
        }
        free(sctx.views);
        for( int t = 0; sctx.rasters && t < sctx.thread_count; t++ )
            raster_scratch_free(&sctx.rasters[t]);
        free(sctx.rasters);
        free(g2.vx);
        free(g2.vy);
        free(g2.vz);
        free(vertex_feature);
        free(dir_x);
        free(dir_y);
        free(dir_z);
        free(off);
        free(best_off);
        free(work_band);
        free(best_band);
    }

    for( int i = 0; i < feature_count; i++ )
        features[i].band = band[i];

    /* ---- report ---- */
    if( do_report )
    {
        int band_faces[HARD_BANDS] = { 0 };
        int band_features[HARD_BANDS] = { 0 };

        printf(
            "geometry: %d vertices, %d faces across %d input model(s)\n",
            g.vertex_count,
            g.face_count,
            input_count);
        printf(
            "features: %d connected\n"
            "baseline: %ld/%ld wrong pixels under the pure depth sort (%.2f%%)\n"
            "          %ld within one feature (no band can reach these), "
            "%ld between features\n"
            "final:    %ld/%ld wrong (%.2f%%), %ld within, %ld between\n"
            "ranked:   %d candidate assignment(s)\n"
            "chosen:   %+ld pixels against the plain depth sort (negative is better)\n",
            feature_count,
            baseline.wrong_within + baseline.wrong_between,
            baseline.covered,
            baseline.covered ? 100.0 *
                                   (double)(baseline.wrong_within + baseline.wrong_between) /
                                   (double)baseline.covered
                             : 0.0,
            baseline.wrong_within,
            baseline.wrong_between,
            tally.wrong_within + tally.wrong_between,
            tally.covered,
            tally.covered
                ? 100.0 * (double)(tally.wrong_within + tally.wrong_between) /
                      (double)tally.covered
                : 0.0,
            tally.wrong_within,
            tally.wrong_between,
            candidate_count,
            -gain);

        for( int i = 0; i < feature_count; i++ )
        {
            band_faces[features[i].band] += features[i].face_count;
            band_features[features[i].band]++;
        }
        printf("%6s %10s %10s\n", "band", "features", "faces");
        for( int b = 0; b < HARD_BANDS; b++ )
            if( band_features[b] )
                printf("%6d %10d %10d\n", b, band_features[b], band_faces[b]);

        printf("\nlargest features:\n%6s %7s %6s\n", "id", "faces", "band");
        {
            unsigned char* shown = (unsigned char*)calloc((size_t)feature_count, 1);
            for( int n = 0; shown && n < 20; n++ )
            {
                int best = -1;
                for( int i = 0; i < feature_count; i++ )
                    if( !shown[i] &&
                        (best < 0 || features[i].face_count > features[best].face_count) )
                        best = i;
                if( best < 0 )
                    break;
                shown[best] = 1;
                printf("%6d %7d %6d\n", best, features[best].face_count, features[best].band);
            }
            free(shown);
        }

        /* Where the between-feature error actually sits, and what a band would
         * cost there. A pair with a large fixable and a comparable breakable is
         * a pair that swaps as the model turns: the error is real, and no band
         * is the answer to it. */
        printf(
            "\nworst feature pairs (last round):\n%6s %6s %8s %8s %8s %5s %5s\n",
            "over",
            "under",
            "fixable",
            "breaks",
            "net",
            "bndA",
            "bndB");
        {
            long* scratch = (long*)malloc(
                (size_t)feature_count * (size_t)feature_count * sizeof(long));
            if( scratch )
            {
                memcpy(
                    scratch,
                    fixable,
                    (size_t)feature_count * (size_t)feature_count * sizeof(long));
                for( int n = 0; n < 15; n++ )
                {
                    int ba = -1, bb = -1;
                    long best = 0;
                    for( int a = 0; a < feature_count; a++ )
                        for( int b = 0; b < feature_count; b++ )
                            if( scratch[(long)a * feature_count + b] > best )
                            {
                                best = scratch[(long)a * feature_count + b];
                                ba = a;
                                bb = b;
                            }
                    if( ba < 0 )
                        break;
                    printf(
                        "%6d %6d %8ld %8ld %8ld %5d %5d\n",
                        ba,
                        bb,
                        best,
                        breakable[(long)ba * feature_count + bb],
                        best - breakable[(long)ba * feature_count + bb],
                        band[ba],
                        band[bb]);
                    scratch[(long)ba * feature_count + bb] = 0;
                }
                free(scratch);
            }
        }

        printf("\nseparations the bands actually buy:\n%6s %6s %10s\n", "over", "under", "net px");
        {
            long threshold = 0;
            for( int n = 0; n < 15; n++ )
            {
                int ba = -1, bb = -1;
                long best = threshold;
                for( int a = 0; a < feature_count; a++ )
                    for( int b = 0; b < feature_count; b++ )
                        if( band[a] > band[b] && net[(long)a * feature_count + b] > best )
                        {
                            best = net[(long)a * feature_count + b];
                            ba = a;
                            bb = b;
                        }
                if( ba < 0 )
                    break;
                printf("%6d %6d %10ld\n", ba, bb, best);
                net[(long)ba * feature_count + bb] = 0;
            }
        }
    }

    /* ---- write ---- */
    for( int i = 0; i < out_count; i++ )
    {
        struct RSCache_Model* m = inputs[i].model;
        struct RSCache_ModelProvenance* p = inputs[i].provenance;
        uint8_t* encoded = NULL;
        uint32_t bound, written;

        /* The slow search's vertex fuzz, in raw coordinates. The analysis
         * geometry was shifted down for version-13+ inputs, so the delta
         * shifts back up on the way out. */
        if( slow_delta_x )
        {
            int const shift = m->format_version >= 13 ? 2 : 0;
            for( int v = 0; v < m->vertex_count; v++ )
            {
                int const gv = inputs[i].vertex_base + v;
                m->vertices_x[v] += slow_delta_x[gv] << shift;
                m->vertices_y[v] += slow_delta_y[gv] << shift;
                m->vertices_z[v] += slow_delta_z[gv] << shift;
            }
        }

        free(m->face_priorities);
        m->face_priorities = (uint8_t*)malloc((size_t)m->face_count);
        if( !m->face_priorities )
            goto done;
        for( int f = 0; f < m->face_count; f++ )
        {
            int const b = features[g.face_feature[inputs[i].face_base + f]].band;
            /* --bulk-flex: the untouched bulk goes to priority 10 rather than
             * band 0. That is not cosmetic. With the bulk flexible, a feature
             * left in a hard band is no longer pinned in front of everything:
             * the sorter splices the flexible run around it at the averaged
             * depth of the occupied hard bands, so the feature sorts AS A UNIT
             * at its own depth -- over the far half of the surface it sits on
             * and under the near half. That is the behaviour a claw ring round
             * a neck actually wants, and no arrangement of hard bands can
             * express it. */
            m->face_priorities[f] = (uint8_t)b;
        }
        m->model_priority = 255;

        /* The encoder prefers the provenance's recorded header over anything
         * derived from the model, so the header's priority byte has to say
         * "per face" (255) or the array it now carries is not written. */
        if( p->header_flag_count > 1 )
            p->header_flags[1] = 255;

        bound = RSCache_ModelEncodeBound(m, p);
        encoded = bound ? (uint8_t*)malloc(bound) : NULL;
        written = encoded ? RSCache_ModelEncodeFormat(m, p, p->format, encoded, bound) : 0;
        if( !written )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot encode %s\n", inputs[i].out_path);
            free(encoded);
            goto done;
        }

        /* Prove the file that lands decodes back to the priorities intended. */
        {
            struct RSCache_ModelProvenance* cp = NULL;
            struct RSCache_Model* check =
                RSCache_ModelNewDecodeProvenance(encoded, (int)written, &cp);
            bool ok = check && cp && check->face_count == m->face_count &&
                      check->face_priorities &&
                      memcmp(check->face_priorities, m->face_priorities,
                             (size_t)m->face_count) == 0;
            RSCache_ModelFree(check);
            RSCache_ModelProvenanceFree(cp);
            if( !ok )
            {
                fprintf(
                    stderr,
                    "rs2012_face_priorities: %s failed its decode check\n",
                    inputs[i].out_path);
                free(encoded);
                goto done;
            }
        }

        if( !write_file(inputs[i].out_path, encoded, written) )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot write %s\n", inputs[i].out_path);
            free(encoded);
            goto done;
        }
        free(encoded);
        printf(
            "rs2012_face_priorities: %s -> %s (%u bytes)\n",
            inputs[i].in_path,
            inputs[i].out_path,
            written);
    }

    rc = 0;

done:
    free(g.vx);
    free(g.vy);
    free(g.vz);
    free(g.fa);
    free(g.fb);
    free(g.fc);
    free(g.face_feature);
    free(features);
    free(feature_face_offset);
    free(feature_faces);
    free(fixable);
    free(breakable);
    if( proposals )
        for( int c = 0; c < candidate_count; c++ )
            free(proposals[c].band);
    free(proposals);
    free(net);
    free(slow_delta_x);
    free(slow_delta_y);
    free(slow_delta_z);
    free(band);
    free(previous);
    free(poses.frames);
    ToriDraw_AnimationFree(poses.anim);
    ToriDraw_ModelFree(poses.model);
    free(r.near_feature);
    free(r.near_z);
    free(r.paint_feature);
    free(r.paint_z);
    free(r.scratch_z);
    free(r.scratch_stamp);
    free(r.touched);
    free(r.sx);
    free(r.sy);
    free(r.sz);
    free(r.face_depth);
    free(r.face_band);
    free(r.face_order);
    for( int i = 0; i < input_count; i++ )
    {
        RSCache_ModelFree(inputs[i].model);
        RSCache_ModelProvenanceFree(inputs[i].provenance);
    }
    return rc;
}
