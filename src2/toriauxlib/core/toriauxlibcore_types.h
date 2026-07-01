#ifndef TORIAUXLIBCORE_TYPES_H
#define TORIAUXLIBCORE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIAUXLIBCORE_MAP_TERRAIN_X 64
#define TORIAUXLIBCORE_MAP_TERRAIN_Z 64
#define TORIAUXLIBCORE_MAP_TERRAIN_LEVELS 4

enum ToriAuxLibCore_Kind
{
    TORIAUXLIBCORE_KIND_MODEL,
    TORIAUXLIBCORE_KIND_ANIMATION,
    TORIAUXLIBCORE_KIND_TEXTURE,
    TORIAUXLIBCORE_KIND_MAP_TERRAIN,
    TORIAUXLIBCORE_KIND_MAP_SCENERY,
    TORIAUXLIBCORE_KIND_FLOTYPE,
    TORIAUXLIBCORE_KIND_UNDERLAY,
    TORIAUXLIBCORE_KIND_LOCATION,
    TORIAUXLIBCORE_KIND_NPCTYPE,
    TORIAUXLIBCORE_KIND_SEQUENCE,
    TORIAUXLIBCORE_KIND_SKELETAL,
    TORIAUXLIBCORE_KIND_SPRITE,
    TORIAUXLIBCORE_KIND_FONT,
    TORIAUXLIBCORE_KIND_COMPONENT,
    TORIAUXLIBCORE_KIND_CLIENTSCRIPT,
    TORIAUXLIBCORE_KIND_OBJTYPE,
    TORIAUXLIBCORE_KIND_COUNT,
};

typedef int16_t gc_faceint_t;
typedef int16_t gc_vertexint_t;
typedef uint16_t gc_hsl16_t;
typedef uint8_t gc_alphaint_t;
typedef uint16_t gc_boneint_t;

struct ToriAuxLibCore_BoundsCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int min_y;
    int max_y;
    int radius;
    int min_z_depth_any_rotation;
};

struct ToriAuxLibCore_Bones
{
    int bones_count;
    gc_boneint_t** bones;
    gc_boneint_t* bones_sizes;
};

struct ToriAuxLibCore_Model
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    gc_vertexint_t* vertices_x;
    gc_vertexint_t* vertices_y;
    gc_vertexint_t* vertices_z;
    gc_hsl16_t* face_colors_a;
    gc_hsl16_t* face_colors_b;
    gc_hsl16_t* face_colors_c;
    gc_faceint_t* face_indices_a;
    gc_faceint_t* face_indices_b;
    gc_faceint_t* face_indices_c;
    gc_faceint_t* face_textures;
    gc_alphaint_t* face_alphas;
    int* face_infos;
    uint8_t* face_priorities;
    gc_hsl16_t* face_colors;
    int textured_face_count;
    gc_faceint_t* textured_p_coordinate;
    gc_faceint_t* textured_m_coordinate;
    gc_faceint_t* textured_n_coordinate;
    gc_faceint_t* face_texture_coords;
    struct ToriAuxLibCore_Bones* vertex_bones;
    struct ToriAuxLibCore_Bones* face_bones;
    struct ToriAuxLibCore_BoundsCylinder* bounds_cylinder;
    struct ToriAuxLibCore_AnimayaSkin* animaya_skin; /* NULL when model has no skeletal skin */
};

struct ToriAuxLibCore_AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** bone_groups;
    uint16_t* bone_group_lengths;
};

struct ToriAuxLibCore_AnimFrame
{
    int id;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct ToriAuxLibCore_Animation
{
    struct ToriAuxLibCore_AnimBase* base;
    struct ToriAuxLibCore_AnimFrame* frames;
    int frame_count;
};

struct ToriAuxLibCore_Texture
{
    int* texels;
    int width;
    int height;
    bool opaque;
    int animation_direction;
    int animation_speed;
    int average_hsl;
};

struct ToriAuxLibCore_MapFloor
{
    uint16_t overlay_id;
    uint8_t underlay_id;
    int16_t height;
    uint8_t settings;
    uint8_t shape;
    uint8_t rotation;
};

struct ToriAuxLibCore_MapTerrain
{
    int map_x;
    int map_z;
    struct ToriAuxLibCore_MapFloor tiles_xyz
        [TORIAUXLIBCORE_MAP_TERRAIN_X * TORIAUXLIBCORE_MAP_TERRAIN_Z *
         TORIAUXLIBCORE_MAP_TERRAIN_LEVELS];
};

struct ToriAuxLibCore_MapLoc
{
    int loc_id;
    int shape_select;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

struct ToriAuxLibCore_MapLocs
{
    int chunk_mapx;
    int chunk_mapz;
    struct ToriAuxLibCore_MapLoc* locs;
    int locs_count;
};

struct ToriAuxLibCore_Flotype
{
    int id;
    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;
};

#define TORIAUXLIBCORE_MENU_ACTION_SLOTS 5
#define TORIAUXLIBCORE_NAME_MAX 32
#define TORIAUXLIBCORE_MENU_ACTION_LEN 32

struct ToriAuxLibCore_Location
{
    int id;
    char name[TORIAUXLIBCORE_NAME_MAX];
    char actions[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN];
    int* shapes;
    int** models;
    int* lengths;
    int shapes_and_model_count;
    int size_x;
    int size_z;
    int blocks_walk;
    int blocks_projectiles;
    int wall_width;
    int seq_id;
    int contoured_ground;
    int contour_ground_type;
    int contour_ground_param;
    int sharelight;
    int shadowed;
    int ambient;
    int contrast;
    int mirrored;
    int resize_x;
    int resize_height;
    int resize_z;
    int offset_x;
    int offset_y;
    int offset_z;
    int* recolors_from;
    int* recolors_to;
    int recolor_count;
    int* retextures_from;
    int* retextures_to;
    int retexture_count;
    int map_scene_id;
    int transform_varbit;
    int transform_varp;
    int* transforms;
    int transform_count;
};

struct ToriAuxLibCore_Npctype
{
    int id;
    char name[TORIAUXLIBCORE_NAME_MAX];
    char actions[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN];
    int combat_level;
    int size;
};

struct ToriAuxLibCore_Objtype
{
    int id;
    char name[TORIAUXLIBCORE_NAME_MAX];
    char inv_actions[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN];
    uint8_t stackable;
};

struct ToriAuxLibCore_Sequence
{
    int id;
    int frame_count;
    int* frames;
    int* iframes;
    int* delay;
    int loops;
    bool stretches;
    int priority;
    int replaceheldleft;
    int replaceheldright;
    int maxloops;
    int preanim_move;
    int postanim_move;
    int duplicate_behavior;
    /** Sequence-ordered animation resolved at world build; owned by this sequence. */
    struct ToriAuxLibCore_Animation* resolved;
    /** Animaya (skeletal) animation id, or -1 if classic frame/framemap. */
    int anim_maya_id;
    /** Skeletal playback range from seq config (animMayaStart/End); duration = end - start. */
    int anim_maya_start;
    int anim_maya_end;
};

/**
 * Neutral representation of a baked Animaya skeletal animation.
 *
 * matrices is a flat array of column-major 4x4 float skinning matrices:
 *   matrices[(frame * bone_count + bone) * 16 .. +15]
 *
 * This is the final skinning matrix: animModelMatrix * invertedModelMatrix(poseId).
 * To skin a vertex: multiply its original position by the weighted sum of the
 * bone matrices for its influences (animaya groups/scales).
 */
struct ToriAuxLibCore_SkeletalAnim
{
    int id;
    int bone_count;
    int frame_count;
    float* matrices; /* [frame_count * bone_count * 16] */
};

/** Per-vertex animaya skin data attached to a model. */
struct ToriAuxLibCore_AnimayaSkin
{
    int vertex_count;
    uint8_t* group_counts; /* [vertex_count] */
    uint8_t** groups;      /* [vertex_count][group_count] bone indices  */
    uint8_t** scales;      /* [vertex_count][group_count] bone weights  */
};

#define TORIAUXLIBCORE_SPRITE_REF_MAX 128
#define TORIAUXLIBCORE_COMPONENT_TEXT_MAX 256
#define TORIAUXLIBCORE_FONT_GLYPH_COUNT 94

struct ToriAuxLibCore_SpriteFrame
{
    uint32_t* pixels_argb;
    int width;
    int height;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
};

struct ToriAuxLibCore_Sprite
{
    char name[64];
    struct ToriAuxLibCore_SpriteFrame* frames;
    int frame_count;
};

struct ToriAuxLibCore_Font
{
    char name[16];
    uint8_t* glyph_alpha[TORIAUXLIBCORE_FONT_GLYPH_COUNT];
    int glyph_width[TORIAUXLIBCORE_FONT_GLYPH_COUNT];
    int glyph_height[TORIAUXLIBCORE_FONT_GLYPH_COUNT];
    int offset_x[TORIAUXLIBCORE_FONT_GLYPH_COUNT];
    int offset_y[TORIAUXLIBCORE_FONT_GLYPH_COUNT];
    int advance[TORIAUXLIBCORE_FONT_GLYPH_COUNT + 1];
    int draw_width[256];
    int line_height;
    char charcodeset[256];
};

enum ToriAuxLibCore_ComponentType
{
    TORIAUXLIBCORE_COMPONENT_LAYER = 0,
    TORIAUXLIBCORE_COMPONENT_INV,
    TORIAUXLIBCORE_COMPONENT_RECT,
    TORIAUXLIBCORE_COMPONENT_TEXT,
    TORIAUXLIBCORE_COMPONENT_GRAPHIC,
    TORIAUXLIBCORE_COMPONENT_MODEL,
    TORIAUXLIBCORE_COMPONENT_INV_TEXT,
    TORIAUXLIBCORE_COMPONENT_LINE,
};

#define TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX 16
/** Matches OSRS INV slotGraphic array length (equipment silhouettes, etc.). */
#define TORIAUXLIBCORE_INV_SLOT_MAX 20

struct ToriAuxLibCore_ScriptHook
{
    int argc;
    int argv[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
};

struct ToriAuxLibCore_ClientScript
{
    int script_id;
    int* instructions;
    int* int_operands;
    int op_count;
    int* cs2vm_ops;
    int cs2vm_op_count;
};

struct ToriAuxLibCore_Component
{
    int id;
    enum ToriAuxLibCore_ComponentType type;
    int parent_id;
    int rel_x;
    int rel_y;
    int width;
    int height;
    /** Client.ts scrollHeight / dat1 scroll. Layer content height; 0 = no vertical scroll. */
    int scroll_height;
    /** dat2 scrollWidth. Layer content width; 0 = no horizontal scroll. */
    int scroll_width;
    char sprite_ref[TORIAUXLIBCORE_SPRITE_REF_MAX];
    /** Client.ts graphic2 / dat activeGraphic: sprite when getIfActive. */
    char sprite_active_ref[TORIAUXLIBCORE_SPRITE_REF_MAX];
    /** MODEL components only. dat1 modelType / dat2 modelType.
     *  Selects what model_id refers to and src2 render path:
     *    0 = none
     *    1 = widget obj model (spellbook runes, static previews) → bake as sprite/graphic
     *    2 = NPC chat head (model_id = npc id) → live 3D draw + anim
     *    3 = player chat head (model_id = appearance hash) → live 3D draw + anim
     *    4 = item on MODEL slot (model_id = obj id) — unsupported initially
     *    5 = local player model (equipment UI) — unsupported initially */
    int model_type;
    /** Widget/NPC/item model index when type==MODEL; meaning depends on model_type. */
    int model_id;
    /** MODEL preview camera: dat2 modelZoom / dat1 zoom. */
    int model_zoom;
    /** MODEL preview pitch: dat2 modelXAngle / dat1 xan. */
    int model_xan;
    /** MODEL preview yaw: dat2 modelYAngle / dat1 yan. */
    int model_yan;
    int color;
    int filled;
    int font_id;
    int center;
    int shadowed;
    /** Client.ts text: inactive label. */
    char text[TORIAUXLIBCORE_COMPONENT_TEXT_MAX];
    /** Client.ts text2 / dat activeText. Used when script comparators pass. */
    char active_text[TORIAUXLIBCORE_COMPONENT_TEXT_MAX];
    int inv_cols;
    int inv_rows;
    int margin_x;
    int margin_y;
    /** LINE widget line thickness (dat2 lineWidth). */
    int line_width;
    /** Per-slot pixel offsets (dat1 invSlotOffsetX/Y). */
    int inv_slot_offset_x[TORIAUXLIBCORE_INV_SLOT_MAX];
    int inv_slot_offset_y[TORIAUXLIBCORE_INV_SLOT_MAX];
    /** Empty-slot background sprite refs (dat1 invSlotGraphic name or dat2 spr:id). */
    char inv_slot_sprite_ref[TORIAUXLIBCORE_INV_SLOT_MAX][TORIAUXLIBCORE_SPRITE_REF_MAX];
    /** Client.ts hide: layer skipped unless hovered_component_id == component id. */
    uint8_t hide;
    int button_type;
    int client_code;
    /** Client.ts overLayerId. dat1 overlayer / dat2 linkedComponentId. -1 = none. */
    int over_layer_id;
    /** Client.ts colourOver: hover tint when inactive. 0 = none. */
    int over_color;
    /** Client.ts colour2: colour when getIfActive. 0 = keep base. */
    int active_color;
    /** Client.ts colour2Over: hover tint when active. 0 = none. */
    int active_over_color;
    int scripts_count;
    int** scripts;
    int* scripts_lengths;
    int* script_comparator;
    int* script_operand;
    uint8_t script_kind;
    struct ToriAuxLibCore_ScriptHook on_load;
    struct ToriAuxLibCore_ScriptHook on_click;
    struct ToriAuxLibCore_ScriptHook on_varp_transmit;
    /** GRAPHIC with no sprite refs: draw nothing, keep layout hitbox. */
    uint8_t graphic_hitbox_only;
    char option[TORIAUXLIBCORE_MENU_ACTION_LEN];
    char ops[TORIAUXLIBCORE_MENU_ACTION_SLOTS][TORIAUXLIBCORE_MENU_ACTION_LEN];
};

void
ToriAuxLibCore_SpriteFrameFree(struct ToriAuxLibCore_SpriteFrame* frame);

void
ToriAuxLibCore_ComponentApplyWalkLayout(
    struct ToriAuxLibCore_Component* component,
    int parent_id,
    int rel_x,
    int rel_y);

void
ToriAuxLibCore_MapTerrainFree(struct ToriAuxLibCore_MapTerrain* terrain);

void
ToriAuxLibCore_MapLocsFree(struct ToriAuxLibCore_MapLocs* locs);

void
ToriAuxLibCore_FlotypeFree(struct ToriAuxLibCore_Flotype* flotype);

void
ToriAuxLibCore_LocationFree(struct ToriAuxLibCore_Location* loc);

void
ToriAuxLibCore_NpctypeFree(struct ToriAuxLibCore_Npctype* npctype);

void
ToriAuxLibCore_ObjtypeFree(struct ToriAuxLibCore_Objtype* objtype);

void
ToriAuxLibCore_SequenceFree(struct ToriAuxLibCore_Sequence* seq);

void
ToriAuxLibCore_ModelFree(struct ToriAuxLibCore_Model* model);

/** Free all owned arrays on model; leaves an empty shell (counts/flags unchanged). */
void
ToriAuxLibCore_ModelReleaseArrays(struct ToriAuxLibCore_Model* model);

void
ToriAuxLibCore_ModelAssertPnmTextureInvariant(struct ToriAuxLibCore_Model const* model);

void
ToriAuxLibCore_AnimationFree(struct ToriAuxLibCore_Animation* anim);

void
ToriAuxLibCore_SkeletalAnimFree(struct ToriAuxLibCore_SkeletalAnim* skeletal);

void
ToriAuxLibCore_AnimayaSkinFree(struct ToriAuxLibCore_AnimayaSkin* skin);

void
ToriAuxLibCore_TextureFree(struct ToriAuxLibCore_Texture* texture);

void
ToriAuxLibCore_SpriteFree(struct ToriAuxLibCore_Sprite* sprite);

void
ToriAuxLibCore_FontFree(struct ToriAuxLibCore_Font* font);

size_t
ToriAuxLibCore_ClientScriptSizeOf(const struct ToriAuxLibCore_ClientScript* script);

void
ToriAuxLibCore_ClientScriptFree(struct ToriAuxLibCore_ClientScript* script);

void
ToriAuxLibCore_ComponentFree(struct ToriAuxLibCore_Component* component);

size_t
ToriAuxLibCore_MapTerrainSizeOf(const struct ToriAuxLibCore_MapTerrain* terrain);

size_t
ToriAuxLibCore_MapLocsSizeOf(const struct ToriAuxLibCore_MapLocs* locs);

size_t
ToriAuxLibCore_FlotypeSizeOf(const struct ToriAuxLibCore_Flotype* flotype);

size_t
ToriAuxLibCore_LocationSizeOf(const struct ToriAuxLibCore_Location* loc);

size_t
ToriAuxLibCore_NpctypeSizeOf(const struct ToriAuxLibCore_Npctype* npctype);

size_t
ToriAuxLibCore_ObjtypeSizeOf(const struct ToriAuxLibCore_Objtype* objtype);

size_t
ToriAuxLibCore_SequenceSizeOf(const struct ToriAuxLibCore_Sequence* seq);

size_t
ToriAuxLibCore_ModelSizeOf(const struct ToriAuxLibCore_Model* model);

size_t
ToriAuxLibCore_AnimationSizeOf(const struct ToriAuxLibCore_Animation* anim);

size_t
ToriAuxLibCore_SkeletalAnimSizeOf(const struct ToriAuxLibCore_SkeletalAnim* skeletal);

size_t
ToriAuxLibCore_AnimayaSkinSizeOf(const struct ToriAuxLibCore_AnimayaSkin* skin);

size_t
ToriAuxLibCore_TextureSizeOf(const struct ToriAuxLibCore_Texture* texture);

size_t
ToriAuxLibCore_SpriteSizeOf(const struct ToriAuxLibCore_Sprite* sprite);

size_t
ToriAuxLibCore_FontSizeOf(const struct ToriAuxLibCore_Font* font);

size_t
ToriAuxLibCore_ComponentSizeOf(const struct ToriAuxLibCore_Component* component);

#endif
