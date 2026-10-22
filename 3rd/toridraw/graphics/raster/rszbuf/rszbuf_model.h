#ifndef TORIDRAW_RSZBUF_MODEL_H
#define TORIDRAW_RSZBUF_MODEL_H

/*
 * What the rev-241 projection hands the rev-241 face walk for one model: the
 * scene's TORIDRAW_SCENE_SCRATCH_RSZBUF group.
 *
 * In the client these are class174's static arrays, shared by every model
 * draw, which is also why they have hard capacities: a model past one of them
 * throws inside the draw and the client catches it and draws nothing. The walk
 * keeps the same limits and the same outcome (RSZBUF_*_CAPACITY).
 *
 * One allocation: the struct and every array after it, so freeing the pointer
 * frees the group.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Vertices and faces per model [class174.field2357 etc., new float[6500]]. */
#define RSZBUF_MODEL_CAPACITY 6500
/** Depth buckets, and faces per bucket [field2433 = new char[6000][512]]. */
#define RSZBUF_DEPTH_BUCKETS 6000
#define RSZBUF_BUCKET_CAPACITY 512
/** Faces per render-priority band [field2367 = new int[12][2500]]. */
#define RSZBUF_PRIORITY_BANDS 12
#define RSZBUF_PRIORITY_CAPACITY 2500

/** The near plane every rev-241 projection and clip uses, in camera z. */
#define RSZBUF_NEAR_PLANE 50

/** Screen x of a vertex behind the near plane [-5000.0F]. */
#define RSZBUF_BEHIND_NEAR_PLANE (-5000.0f)

/* face_flags bits [field2356, field2354, field2370]. */
#define RSZBUF_FACE_CULLED (1u << 0)
#define RSZBUF_FACE_CLIP_X (1u << 1)
#define RSZBUF_FACE_NEAR_CLIPPED (1u << 2)

struct ToriDraw_RsZbufModel
{
    int vertex_capacity;
    int face_capacity;

    /* The colour and depth write masks the rasterizer holds between draws
     * [class175.field2465 / field2442]. A model sets them before it draws and
     * every mode leaves them -1, -1 -- unless its draw throws part way, when
     * the next tile inherits whatever was live. Kept here so that holds. */
    int color_write_mask;
    int depth_write_mask;

    /* Per model, from the projection [class174.method5690 / method5784]. */
    /** Camera-space depth of the model origin [var18]. */
    int center_depth;
    /** XZ radius, depth of the lowest point, height [field2419/2418/2037]. */
    int radius;
    int bottom;
    int height;
    /** Bucket of the nearest possible face, and how many buckets the model
     *  can span [field2421, field2420]. */
    int depth_offset;
    int depth_range;
    /** The screen centre and integer zoom the vertices were projected with
     *  [class175.field2454 / field2455 / field2452]; the near clip projects
     *  its new points with the same three. */
    int center_x;
    int center_y;
    int projection_scale;
    /** Some vertex is behind the near plane [method5690 var31]. */
    bool any_behind_near;
    /** Camera-space ints were stored [var33: textured, or near enough to
     *  clip]. */
    bool camera_stored;

    /* Per vertex. */
    float* screen_x;     /* [field2357]; RSZBUF_BEHIND_NEAR_PLANE when behind */
    float* screen_y;     /* [field2409] */
    float* depth;        /* camera z [field2406] */
    int* bucket_depth;   /* (int) z - center_depth [field2391] */
    int* camera_x;       /* (int) camera-space position [field2368] */
    int* camera_y;       /* [field2362] */
    int* camera_z;       /* [field2398] */

    /* Per face. */
    uint8_t* face_flags;

    /* The walk's ordering scratch. */
    int* bucket_count;   /* RSZBUF_DEPTH_BUCKETS */
    int* bucket_start;   /* RSZBUF_DEPTH_BUCKETS */
    int* face_order;     /* face_capacity, faces grouped by bucket */
    int* priority_faces; /* RSZBUF_PRIORITY_BANDS x RSZBUF_PRIORITY_CAPACITY */
    int* priority_depth; /* bucket of each face in bands 10 and 11, same shape */
};

/** Bytes for the group at these capacities, header included. */
static inline size_t
toridraw_rszbuf_model_bytes(int vertex_capacity, int face_capacity)
{
    size_t const vertices = (size_t)vertex_capacity;
    size_t const faces = (size_t)face_capacity;

    return sizeof(struct ToriDraw_RsZbufModel) + vertices * 3 * sizeof(float) +
           vertices * 4 * sizeof(int) + faces * sizeof(uint8_t) + 16 +
           (size_t)RSZBUF_DEPTH_BUCKETS * 2 * sizeof(int) + faces * sizeof(int) +
           (size_t)RSZBUF_PRIORITY_BANDS * RSZBUF_PRIORITY_CAPACITY * 2 * sizeof(int);
}

/** Lay the arrays out after the header of a toridraw_rszbuf_model_bytes block. */
static inline void
toridraw_rszbuf_model_carve(
    struct ToriDraw_RsZbufModel* model,
    int vertex_capacity,
    int face_capacity)
{
    char* at = (char*)(model + 1);
    size_t const vertices = (size_t)vertex_capacity;
    size_t const faces = (size_t)face_capacity;
    size_t const priorities = (size_t)RSZBUF_PRIORITY_BANDS * RSZBUF_PRIORITY_CAPACITY;

#define RSZBUF_CARVE(field, type, count)                                                           \
    model->field = (type*)(void*)at;                                                               \
    at += (count) * sizeof(type)

    model->vertex_capacity = vertex_capacity;
    model->face_capacity = face_capacity;
    RSZBUF_CARVE(screen_x, float, vertices);
    RSZBUF_CARVE(screen_y, float, vertices);
    RSZBUF_CARVE(depth, float, vertices);
    RSZBUF_CARVE(bucket_depth, int, vertices);
    RSZBUF_CARVE(camera_x, int, vertices);
    RSZBUF_CARVE(camera_y, int, vertices);
    RSZBUF_CARVE(camera_z, int, vertices);
    RSZBUF_CARVE(bucket_count, int, (size_t)RSZBUF_DEPTH_BUCKETS);
    RSZBUF_CARVE(bucket_start, int, (size_t)RSZBUF_DEPTH_BUCKETS);
    RSZBUF_CARVE(face_order, int, faces);
    RSZBUF_CARVE(priority_faces, int, priorities);
    RSZBUF_CARVE(priority_depth, int, priorities);
    RSZBUF_CARVE(face_flags, uint8_t, faces);
#undef RSZBUF_CARVE
}

#endif
