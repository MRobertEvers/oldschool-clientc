#ifndef TORIDRAW_TYPES_H
#define TORIDRAW_TYPES_H

#include "graphics/projection.h"
#include "graphics/zdepth.h"
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

/* TORIDRAW_SCREEN_X_NEAR_CLIPPED and its nudge live in graphics/projection.h,
 * with the kernels that write them; included above via that header. */

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

/**
 * Draw this model's own faces through the depth-tested kernels instead of
 * relying on the depth sort alone.
 *
 * Opt-in, and deliberately per model rather than per scene: the painter's sort
 * is what the content was authored against, and it is right for the
 * overwhelming majority of models. It is wrong for a model whose parts
 * genuinely interpenetrate — a wing through a body, a jaw through a skull —
 * because no single order over whole faces can express "these two triangles
 * each occlude the other". Those models set this and get the per-pixel answer.
 *
 * The scope is one model. The buffer is cleared before the model's faces and
 * never consulted across models, so this changes nothing about how the model
 * layers against the rest of the scene. See graphics/zdepth.h.
 *
 * Requires the scene to carry the depth scratch: TORIDRAW_SCENE_MODEL_ZBUFFER
 * at ToriDraw_SceneNew, or an explicit ToriDraw_SceneModelZBufferResize. With
 * no buffer the flag is inert and the model draws exactly as it did before.
 */
#define TORIDRAW_MODEL_FLAG_ZBUFFER ((uint8_t)(1u << 0))

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
    /** Which of the two knobs below drives the projection. Zero-initialising a
     *  camera selects SCALE, so a memset camera projects at the reference's
     *  default 512 rather than at whatever an unset angle would resolve to. */
    enum ToriDraw_ProjMode proj_mode;

    /** The reference client's viewport scale (class159.method5357 ->
     *  client.field817): the integer multiplier in screen = coord * scale / z,
     *  recomputed per layout from the world viewport height. Live when
     *  proj_mode == TORIDRAW_PROJ_MODE_SCALE. 0 = TORIDRAW_PROJ_SCALE_DEFAULT.
     *
     *  The only way to match a reference projection exactly -- most integer
     *  scales are not reachable through fov_rpi2048 at all. */
    int proj_scale;

    /** Field of view, in units of 2*pi/2048. Live when proj_mode ==
     *  TORIDRAW_PROJ_MODE_FOV. 0 = TORIDRAW_PROJ_FOV_DEFAULT. Natural for a
     *  free camera; lossy as a way to request a specific scale (see the ladder
     *  note in graphics/projection.h). */
    int fov_rpi2048;

    /**
     * Pixels per world unit, 16.16 fixed point. Live when proj_mode ==
     * TORIDRAW_PROJ_MODE_PARALLEL; TORIDRAW_ORTHO_ZOOM_UNIT (65536) is 1:1.
     * Deliberately not proj_scale reused: that one is a perspective numerator
     * measured against z, this is a plain screen scale, and collapsing two
     * different quantities into one field is how a camera ends up projecting
     * with a value nobody set.
     */
    int parallel_zoom16;

    /**
     * Depth at which geometry stops being drawn.
     *
     * Under perspective this is a hard requirement -- it is the divide's
     * singularity. Under parallel projection nothing is unsafe, so it becomes a
     * policy knob: geometry nearer than this is hidden, which is what stops a
     * map-editor camera sitting inside a wall from drawing the wall behind it.
     * Set it to a large negative value to disable near clipping entirely and
     * keep every model on the cheaper no-clip kernels.
     */
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
    /*
     * Each texel carries its own alpha in bits 24-31 and is blended over the
     * framebuffer, instead of the stock colour key where a texel is drawn or
     * skipped whole.
     *
     * Stock content never sets this. It exists for imported material that
     * varies continuously in alpha, which a colour key can only represent by
     * thresholding into holes the source never had.
     */
    bool alpha_blended;
    int animation_direction;
    int animation_speed;
};

/*
 * Texture id capacity. 256 held for the sprite-backed eras, but the RS2 procedural
 * materials number to 1163 in cache.643 — and 234 of its SD-drawable materials sit above
 * 255, so a 256-slot map silently never draws them (the raster skips faces whose texture
 * is absent). 2048 covers the era with headroom; the map is pointers, so the cost is 16KB.
 */
#define TORIDRAW_TEXTURE_ID_CAPACITY 2048

struct ToriDraw_TextureMap
{
    struct ToriDraw_Texture* textures[TORIDRAW_TEXTURE_ID_CAPACITY];
    int count;
};

struct ToriDraw_Animation;
struct ToriDraw_Map;
struct ToriDraw_Vec;
struct ToriDraw_ScenePendingPose;
struct ToriDraw_Sprite;
struct ToriDraw_Font;
struct ToriDraw_Sound;

/*
 * A decoded sound clip held by the scene.
 *
 * Sound is an *asset*, exactly like a model or a sprite: the game decodes it
 * once, hands it to the scene under an id, and the scene's load/unload events
 * carry it to whichever backend is listening. Keeping it here rather than in a
 * private cache is what lets one rule -- "the scene owns loaded assets, and
 * clearing the scene unloads them" -- cover audio too, so an area sound cannot
 * outlive the map it belongs to.
 *
 * PCM is 16-bit signed mono. `loop_start`/`loop_end` are sample offsets and are
 * the clip's own loop span, not a playback choice; whether a given play loops is
 * decided per voice.
 */
struct ToriDraw_Sound
{
    int16_t* samples; /* owned */
    int sample_count;
    int sample_rate;
    int loop_start;
    int loop_end;
    bool ping_pong;
    /** Ticks of silence trimmed off the front (the reference's Wave.trim), which
     *  the game adds back to the server's play delay. Carried with the clip
     *  because it is a property of the clip, not of a play. */
    int queue_delay;
};

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
    TORIDRAW_EVENT_SOUND_LOAD,
    TORIDRAW_EVENT_SOUND_UNLOAD,
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
    struct ToriDraw_Sound* sound;
    int sound_id;
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
    /** Clear group (TORIDRAW_SCENE_POOL_*): STATIC elements are freed
     *  wholesale by ToriDraw_SceneClearPool on a map rebuild; DYNAMIC
     *  elements (entities) keep their ids across it. */
    uint8_t pool;
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
    /** Entries in each flexible-priority (10/11) array. The two are allocated
     *  the same size; the sorter merges 11 into 10, so the merged run must fit
     *  this many entries. Kept so the sort can assert that rather than trust
     *  it: these arrays sit next to the other scratch, and an overrun lands in
     *  a live neighbour instead of anywhere a sanitizer can see. */
    int flex_prio_capacity;

    struct ToriDraw_ModelHandle active_hnd;

    struct ProjectedVertex projected_vertex;
    struct ToriDraw_AABB aabb;
    struct ToriDraw_AABB cylinder_fast_aabb;

    /*
     * Whether the model ToriDraw_Project last projected could reach behind the
     * near plane, and so whether screen_vertices_x may hold
     * TORIDRAW_SCREEN_X_NEAR_CLIPPED. False for the overwhelming majority of
     * models: the camera has to be inside the model's bounding sphere for a
     * vertex to clip. Consumers that test for the sentinel (the triangle
     * dispatchers, the per-face pick) must check this first, both to skip the
     * test entirely in the common case and because the no-clip kernel does not
     * nudge a genuine -5000 out of the way. Mirrors `clipped` in the reference
     * (Client-TS Model.worldRender:1755, consumed at render2:1876).
     */
    bool near_clipped;

    /*
     * Near plane ToriDraw_Project actually used for the model it last
     * projected, which is camera->near_plane_z raised far enough that no
     * projected coordinate can leave the rasterizer's 16.16 domain. See
     * toridraw_safe_near_plane_z. Every consumer of the projection scratch —
     * the near-clip triangle builders above all — must clip against this
     * value, not the camera's, or the two disagree about where the plane is.
     */
    int projection_near_plane_z;

    struct ToriDraw_TextureState* tex_state;

    /*
     * Per-model depth scratch, screen sized. NULL unless the scene was created
     * with TORIDRAW_SCENE_MODEL_ZBUFFER (allocated lazily, on the first raster
     * of a model that opts in) or a caller sized it up front. Only models
     * carrying TORIDRAW_MODEL_FLAG_ZBUFFER read or write it, and only within
     * their own raster pass — see graphics/zdepth.h for why the scope stops
     * there.
     *
     * `model_zbuffer_stride` is the row stride in ELEMENTS and matches the
     * viewport stride the buffer was sized for, so a pixel at `offset` in the
     * frame buffer is at the same `offset` here. Keeping the two layouts
     * identical is what lets one offset walk both.
     */
    torizdepth_t* model_zbuffer;
    int model_zbuffer_stride;
    int model_zbuffer_rows;

    int* screen_vertices_x;
    int* screen_vertices_y;
    int* screen_vertices_z;
    int* orthographic_vertices_x;
    int* orthographic_vertices_y;
    int* orthographic_vertices_z;

    faceint_t* tmp_depth_face_count;
    faceint_t* tmp_depth_faces;
    faceint_t* tmp_priority_face_count;
    /* Sum of face depths per priority, for the flexible-priority averages in
     * sort_face_draw_order. **int, not faceint_t**: this accumulates, where
     * every other scratch array here holds one face index. A depth is 0..1499
     * and a model can have hundreds of faces at one priority, so an int16 wraps
     * — 451 faces at priority 4 on npc 999 sum past 32767 long before the
     * average is taken. The reference holds these in an int array for the same
     * reason. */
    int* tmp_priority_depth_sum;
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
    /** Decoded sound clips by id — see ToriDraw_SceneSoundAdd. */
    struct ToriDraw_Map* sounds_hmap;
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

    /*
     * Ids of elements the per-cycle animation tick has to visit — those with a
     * seq bound that is not externally driven. The pool is dominated by static
     * scenery, so scanning every slot each cycle to find the handful of
     * animated ones was pure overhead; this list is rebuilt lazily whenever
     * anim_list_dirty is set. See ToriDraw_SceneAnimatedElements.
     *
     * Entries are only a hint: consumers still re-check liveness and
     * anim_seq_id, so a stale id is harmless. A *missing* id is what matters,
     * hence every element alloc/release/clear and every seq mutation marks the
     * list dirty.
     */
    int* anim_list;
    int anim_list_count;
    int anim_list_cap;
    bool anim_list_dirty;
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
