#ifndef TORIDRAW_TYPES_H
#define TORIDRAW_TYPES_H

#include "graphics/projection.h"
#include "toridraw_intrusive_list.h"

#include <stdbool.h>
#include <stdint.h>

typedef int16_t faceint_t;
typedef int16_t vertexint_t;
typedef uint16_t hsl16_t;
typedef uint8_t alphaint_t;
typedef uint16_t boneint_t;

/** Map dat2 raw per-face texture coord to renderer form (-1 = none, else PNM index). */
static inline faceint_t
ToriDraw_NormalizeFaceTextureCoord(int raw, int textured_face_count)
{
    int const idx = raw & 255;
    if( idx == 255 )
        return -1;
    if( textured_face_count <= 0 || idx >= textured_face_count )
        return -1;
    return (faceint_t)idx;
}

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
    /* Uniform priority for models with no per-face array; consumed by ToriDraw_ModelNewMerge so
     * merged parts keep their layering. See ToriRS_Model.model_priority. */
    uint8_t model_priority;
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

    /* Animaya per-vertex skin (NULL if no skeletal rigging) */
    int      animaya_vertex_count;
    uint8_t* animaya_group_counts; /* [animaya_vertex_count] */
    uint8_t** animaya_groups;      /* [animaya_vertex_count][count] bone indices */
    uint8_t** animaya_scales;      /* [animaya_vertex_count][count] weights 0-255 */
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
struct ToriDraw_ScenePendingPose;
struct ToriDraw_Sprite;
struct ToriDraw_Font;

#define TORIDRAW_SCENE_INVALID_BATCH_ID (-1)
#define TORIDRAW_SCENE_INVALID_ELEMENT_ID (-1)
#define TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE 65536
#define TORIDRAW_SCENE_MAX_ELEMENTS 65536

enum ToriDraw_EventKind
{
    TORIDRAW_EVENT_NONE = 0,
    TORIDRAW_EVENT_MODEL_LOAD,
    TORIDRAW_EVENT_MODEL_UNLOAD,
    TORIDRAW_EVENT_ANIM_LOAD,
    TORIDRAW_EVENT_ANIM_UNLOAD,
    TORIDRAW_EVENT_TEX_LOAD,
    TORIDRAW_EVENT_TEX_UNLOAD,
    TORIDRAW_EVENT_SPRITE_LOAD,
    TORIDRAW_EVENT_SPRITE_UNLOAD,
    TORIDRAW_EVENT_FONT_LOAD,
    TORIDRAW_EVENT_FONT_UNLOAD,
    TORIDRAW_EVENT_BATCH_BEGIN,
    TORIDRAW_EVENT_BATCH_MODEL_ADD,
    TORIDRAW_EVENT_BATCH_ANIM_ADD,
    TORIDRAW_EVENT_BATCH_END,
    TORIDRAW_EVENT_BATCH_CLEAR,
    TORIDRAW_EVENT_SCENE_RESET,
};

struct ToriDraw_Event
{
    enum ToriDraw_EventKind kind;
    int batch_id;
    int element_id;
    int pose_id;
    int anim_index;
    int texture_id;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Texture* texture;
    struct ToriDraw_Sprite** sprites;
    int sprite_count;
    struct ToriDraw_Font* font;
    struct ToriDraw_Position world_position;
};

struct ToriDraw_EventQueue
{
    struct ToriDraw_Event events[TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE];
    int count;
};

struct ToriDraw_TextureState
{
    struct ToriDraw_TextureMap texture_map;
};

struct ToriDraw_SkeletalAnim;

struct ToriDraw_SceneElement
{
    int scene_id;
    struct ToriDraw_ModelHandle model;
    struct ToriDraw_Animation* animation;
    struct ToriDraw_Animation* secondary_animation;
    struct ToriDraw_SkeletalAnim* skeletal_animation;
    struct ToriDraw_Position world_position;
    bool dynamic;
    bool pending_batch_add;
    bool is_skeletal; /* true: use skeletal_animation; false: use animation */
    int skeletal_play_frames; /* playback length from seq anim_maya_end-start; -1 = palette size */
    int anim_seq_id;
    int anim_frame;
    int anim_cycle;

    /* Secondary (walk) track for the walkmerge blend: when the primary
     * animation carries a walkmerge mask and anim2 is bound, the apply pass
     * blends primary+secondary via ToriDraw_ModelAnimateFrameMasked. */
    int anim2_seq_id; /* -1/0 = no secondary track */
    int anim2_frame;
    /** Entity-owned animation stepping: the app's world sim drives
     * anim_frame/anim2_frame; the generic per-element modulo tick skips
     * elements with this set. */
    bool anim_external;
    /** Reference Model.useAABBMouseCheck: pick against the projected bounding
     *  box instead of per-face. Set for npcs, players and ground objs (see
     *  ObjType.getWorldModel / ClientPlayer / NpcType); locs keep the exact
     *  per-face test. */
    bool pick_aabb;
};

struct ToriDraw_SceneBatchElementHandle
{
    struct ToriDraw_Scene* scene;
    int batch_id;
    int id;
};

#define TORIDRAW_CACHE_FONT_SLOT_COUNT 4

struct ToriDraw_Scene
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

    struct ToriDraw_EventQueue event_queue;
    struct ToriDraw_Map* models_hmap;
    struct ToriDraw_Map* animation_hmap;
    struct ToriDraw_Map* sprites_hmap;
    struct ToriDraw_Map* fonts_hmap;
    /** Always-resident cache fonts indexed by revconfig cache_font_id 0–3. */
    struct ToriDraw_Font* cache_fonts[TORIDRAW_CACHE_FONT_SLOT_COUNT];
    struct ToriDraw_IntrusiveList elements;

    bool batch_building;
    int current_batch_id;
    int current_batch_element_count;
    int next_batch_id;

    struct ToriDraw_ScenePendingPose* pending_poses;
    int pending_pose_count;
    int pending_pose_cap;
};

#define TORIDRAW_CULL_VISIBLE 0
#define TORIDRAW_CULL_FAST 1
#define TORIDRAW_CULL_AABB 2
#define TORIDRAW_CULL_ERROR 3

static inline int*
ToriDraw_FaceOrder(struct ToriDraw_Scene* scene)
{
    return scene->tmp_face_order;
}

static inline int
ToriDraw_FaceOrderCount(struct ToriDraw_Scene* scene)
{
    return scene->tmp_face_order_count;
}

#endif
