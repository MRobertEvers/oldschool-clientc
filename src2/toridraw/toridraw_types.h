#ifndef TORIDRAW_TYPES_H
#define TORIDRAW_TYPES_H

#include "graphics/projection.h"

#include <stdbool.h>
#include <stdint.h>

typedef int16_t faceint_t;
typedef int16_t vertexint_t;
typedef uint16_t hsl16_t;
typedef uint8_t alphaint_t;
typedef uint16_t boneint_t;

/** Sentinels for channel C in lit face colors; lightness=127 is never produced by lighting. */
#define TORIDRAWHSL16_HIDDEN ((hsl16_t)0xFFFF)
#define TORIDRAWHSL16_FLAT ((hsl16_t)0xFF7F)

struct ToriDraw_BoundsCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int min_y;
    int max_y;
    int radius;

    // TODO: Name?
    // - Max extent from model origin.
    // - Distance to farthest vertex?
    int min_z_depth_any_rotation;
};

#define TORIDRAW_AABB_KIND_CYLINDER_4POINT 0
#define TORIDRAW_AABB_KIND_CYLINDER_8POINT 1

struct ToriDraw_AABB
{
    int kind;
    int min_screen_x;
    int min_screen_y;
    int max_screen_x;
    int max_screen_y;
};

struct ToriDraw_Normal
{
    int16_t x;
    int16_t y;
    int16_t z;
    uint16_t face_count;
    uint8_t merged;
};

struct ToriDraw_Normals
{
    struct ToriDraw_Normal* vertex_normals;
    int vertex_normals_count;

    struct ToriDraw_Normal* face_normals;
    int face_normals_count;
};

struct ToriDraw_Bones
{
    int bones_count;
    boneint_t** bones;
    boneint_t* bones_sizes;
};

struct ToriDraw_Model
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;

    vertexint_t* original_vertices_x;
    vertexint_t* original_vertices_y;
    vertexint_t* original_vertices_z;

    alphaint_t* face_alphas;
    alphaint_t* original_face_alphas;
    int* face_infos;
    /** Two 4-bit priorities per byte (low nibble = even face index). Values 0–12. */
    uint8_t* face_priorities;
    hsl16_t* face_colors;

    int textured_face_count;
    faceint_t* textured_p_coordinate;
    faceint_t* textured_m_coordinate;
    faceint_t* textured_n_coordinate;
    faceint_t* face_texture_coords;

    struct ToriDraw_Normals* normals;
    struct ToriDraw_Normals* merged_normals;
    struct ToriDraw_Bones* vertex_bones;
    struct ToriDraw_Bones* face_bones;
    struct ToriDraw_BoundsCylinder* bounds_cylinder;
};

struct ToriDraw_ModelGround
{
    int vertex_count;
    int face_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;
};

enum ToriDraw_ModelKind
{
    TORIDRAWMK_NONE = 0,
    TORIDRAWMK_MODEL = 1,
    TORIDRAWMK_GROUND = 2,
};

struct ToriDraw_ModelHandle
{
    enum ToriDraw_ModelKind kind;
    union
    {
        struct
        {
            struct ToriDraw_Model* model;
            struct ToriDraw_ModelGround* ground;
        } model;
    } u;
};

struct ToriDraw_Position
{
    int x;
    int y;
    int z;
    int pitch;
    int yaw;
    int roll;
};

struct ToriDraw_ViewPort
{
    int width;
    int height;
    int stride;

    int x_center;
    int y_center;

    int clip_left;
    int clip_top;
    int clip_right;
    int clip_bottom;
};

struct ToriDraw_Camera
{
    int fov_rpi2048;
    int near_plane_z;

    int pitch;
    int yaw;
    int roll;
};

enum ToriDraw_TextureAnimation
{
    TORIDRAW_TEXANIM_DIRECTION_NONE,
    TORIDRAW_TEXANIM_DIRECTION_V_DOWN = 1,
    TORIDRAW_TEXANIM_DIRECTION_U_DOWN = 2,
    TORIDRAW_TEXANIM_DIRECTION_V_UP = 3,
    TORIDRAW_TEXANIM_DIRECTION_U_UP = 4,
};

struct ToriDraw_Texture
{
    int* texels;
    int width;
    int height;
    bool opaque;
    int animation_direction;
    int animation_speed;
};

struct ToriDraw_TextureMap
{
    struct ToriDraw_Texture* textures[256];
    int count;
};

struct ToriDraw_Animation;
struct ToriDraw_Map;
struct ToriDraw_Vec;
struct ToriDraw_GCPendingPose;

#define TDGC_INVALID_BATCH_ID (-1)
#define TDGC_INVALID_ELEMENT_ID (-1)
#define TDGC_EVENT_QUEUE_MAX_SIZE 65536
#define TDGC_MAX_ELEMENTS 65536

enum ToriDraw_GCEventKind
{
    TDGC_NONE = 0,
    TDGC_MODEL_LOAD,
    TDGC_MODEL_UNLOAD,
    TDGC_ANIM_LOAD,
    TDGC_ANIM_UNLOAD,
    TDGC_TEX_LOAD,
    TDGC_TEX_UNLOAD,
    TDGC_BATCH_BEGIN,
    TDGC_BATCH_MODEL_ADD,
    TDGC_BATCH_ANIM_ADD,
    TDGC_BATCH_END,
    TDGC_BATCH_CLEAR,
    TDGC_SCENE_RESET,
};

struct ToriDraw_GCEvent
{
    enum ToriDraw_GCEventKind kind;
    int batch_id;
    int element_id;
    int pose_id;
    int texture_id;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Texture* texture;
    struct ToriDraw_Position world_position;
};

struct ToriDraw_GCEventQueue
{
    struct ToriDraw_GCEvent events[TDGC_EVENT_QUEUE_MAX_SIZE];
    int count;
};

enum ToriDraw_EventKind
{
    TORIDRAW_EVENT_NONE = 0,
    TORIDRAW_EVENT_TEX_LOAD,
    TORIDRAW_EVENT_TEX_UNLOAD,
};

struct ToriDraw_Event
{
    enum ToriDraw_EventKind kind;
    int texture_id;
    struct ToriDraw_Texture* texture;
};

#define TORIDRAW_EVENT_QUEUE_MAX 256

struct ToriDraw_EventQueue
{
    struct ToriDraw_Event events[TORIDRAW_EVENT_QUEUE_MAX];
    int count;
};

struct ToriDraw_TextureState
{
    struct ToriDraw_TextureMap texture_map;
    struct ToriDraw_EventQueue events;
};

struct ToriDraw_GCElement
{
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Animation* secondary_animation;
    struct ToriDraw_Position world_position;
    bool pending_batch_add;
    int anim_seq_id;
    int anim_frame;
    int anim_cycle;
};

struct ToriDraw_GCBatchElementHandle
{
    struct ToriDraw_Context* context;
    int batch_id;
    int id;
};

struct ToriDraw_Context
{
    uint32_t flags;
    int max_vertices;
    int max_faces;
    int depth_levels;
    int depth_stride;
    int priority_stride;

    struct ToriDraw_ModelHandle active_hnd;

    struct ProjectedVertex projected_vertex;
    struct ToriDraw_AABB aabb;
    struct ToriDraw_AABB cylinder_fast_aabb;

    struct ToriDraw_TextureState* tex_state;

    int* screen_vertices_x;
    int* screen_vertices_y;
    int* screen_vertices_z;
    int* orthographic_vertices_x;
    int* orthographic_vertices_y;
    int* orthographic_vertices_z;

    faceint_t* tmp_depth_face_count;
    faceint_t* tmp_depth_faces;
    faceint_t* tmp_priority_face_count;
    faceint_t* tmp_priority_depth_sum;
    faceint_t* tmp_priority_faces;
    int* tmp_flex_prio11_face_to_depth;
    int* tmp_flex_prio12_face_to_depth;

    faceint_t* sm_face_depth;
    int* sm_depth_offset;
    int* sm_depth_cursor;
    faceint_t* sm_faces_by_depth;
    int sm_prio_count[13];
    int* sm_prio_offset;
    faceint_t* sm_prio_faces;
    int* sm_flex_prio11_face_to_depth;
    int* sm_flex_prio12_face_to_depth;

    int* tmp_face_order;
    int tmp_face_order_count;

    faceint_t sparse_a[4096];
    faceint_t sparse_b[4096];
    faceint_t sparse_c[4096];

    struct ToriDraw_GCEventQueue event_queue;
    struct ToriDraw_Map* models_hmap;
    struct ToriDraw_Map* animation_hmap;
    struct ToriDraw_Vec* elements;

    bool slots[TDGC_MAX_ELEMENTS];
    int slot_count;

    int free_list[TDGC_MAX_ELEMENTS];
    int free_count;

    bool batch_building;
    int current_batch_id;
    int current_batch_element_count;
    int next_batch_id;

    struct ToriDraw_GCPendingPose* pending_poses;
    int pending_pose_count;
    int pending_pose_cap;
};

#define TORIDRAW_CULL_VISIBLE 0
#define TORIDRAW_CULL_FAST 1
#define TORIDRAW_CULL_AABB 2
#define TORIDRAW_CULL_ERROR 3

static inline int*
toridraw_face_order(struct ToriDraw_Context* ctx)
{
    return ctx->tmp_face_order;
}

static inline int
toridraw_face_order_count(struct ToriDraw_Context* ctx)
{
    return ctx->tmp_face_order_count;
}

static inline void
toridraw_eventqueue_clear(struct ToriDraw_EventQueue* queue)
{
    if( !queue )
        return;
    queue->count = 0;
}

static inline bool
toridraw_eventqueue_push(
    struct ToriDraw_EventQueue* queue,
    enum ToriDraw_EventKind kind,
    int texture_id,
    struct ToriDraw_Texture* texture)
{
    if( !queue || kind == TORIDRAW_EVENT_NONE || texture_id < 0 || texture_id >= 256 )
        return false;
    if( queue->count >= TORIDRAW_EVENT_QUEUE_MAX )
        return false;
    struct ToriDraw_Event* ev = &queue->events[queue->count++];
    ev->kind = kind;
    ev->texture_id = texture_id;
    ev->texture = texture;
    return true;
}

#endif