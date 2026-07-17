#ifndef TORIRS_TYPES_H
#define TORIRS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIRS_MAP_TERRAIN_X 64
#define TORIRS_MAP_TERRAIN_Z 64
#define TORIRS_MAP_TERRAIN_LEVELS 4

enum ToriRS_Kind
{
    TORIRS_KIND_MODEL,
    TORIRS_KIND_ANIMATION,
    TORIRS_KIND_TEXTURE,
    TORIRS_KIND_MAP_TERRAIN,
    TORIRS_KIND_MAP_SCENERY,
    TORIRS_KIND_FLOTYPE,
    TORIRS_KIND_UNDERLAY,
    TORIRS_KIND_LOCATION,
    TORIRS_KIND_NPCTYPE,
    TORIRS_KIND_SEQUENCE,
    // Dat2
    TORIRS_KIND_SKELETAL,
    TORIRS_KIND_SPRITE,
    TORIRS_KIND_FONT,
    TORIRS_KIND_COMPONENT,
    // Dat2
    TORIRS_KIND_COMPONENT_PACK,
    TORIRS_KIND_CLIENTSCRIPT,
    TORIRS_KIND_OBJTYPE,
    TORIRS_KIND_COUNT,
};

typedef int16_t gc_faceint_t;
typedef int16_t gc_vertexint_t;
typedef uint16_t gc_hsl16_t;
typedef uint8_t gc_alphaint_t;
typedef uint16_t gc_boneint_t;

struct ToriRS_BoundsCylinder
{
    int center_to_top_edge;
    int center_to_bottom_edge;
    int min_y;
    int max_y;
    int radius;
    int min_z_depth_any_rotation;
};

struct ToriRS_Bones
{
    int bones_count;
    gc_boneint_t** bones;
    gc_boneint_t* bones_sizes;
};

struct ToriRS_Model
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
    struct ToriRS_Bones* vertex_bones;
    struct ToriRS_Bones* face_bones;
    struct ToriRS_BoundsCylinder* bounds_cylinder;
    struct ToriRS_AnimayaSkin* animaya_skin; /* NULL when model has no skeletal skin */
};

struct ToriRS_AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** bone_groups;
    uint16_t* bone_group_lengths;
};

struct ToriRS_AnimFrame
{
    int id;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct ToriRS_Animation
{
    struct ToriRS_AnimBase* base;
    struct ToriRS_AnimFrame* frames;
    int frame_count;
};

struct ToriRS_Texture
{
    int* texels;
    int width;
    int height;
    bool opaque;
    int animation_direction;
    int animation_speed;
    int average_hsl;
};

struct ToriRS_MapFloor
{
    uint16_t overlay_id;
    uint8_t underlay_id;
    int16_t height;
    uint8_t settings;
    uint8_t shape;
    uint8_t rotation;
};

struct ToriRS_MapTerrain
{
    int map_x;
    int map_z;
    struct ToriRS_MapFloor
        tiles_xyz[TORIRS_MAP_TERRAIN_X * TORIRS_MAP_TERRAIN_Z * TORIRS_MAP_TERRAIN_LEVELS];
};

struct ToriRS_MapLoc
{
    int loc_id;
    int shape_select;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

struct ToriRS_MapLocs
{
    int chunk_mapx;
    int chunk_mapz;
    struct ToriRS_MapLoc* locs;
    int locs_count;
};

struct ToriRS_Flotype
{
    int id;
    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;
};

#define TORIRS_MENU_ACTION_SLOTS 5
#define TORIRS_NAME_MAX 32
#define TORIRS_MENU_ACTION_LEN 32

struct ToriRS_Location
{
    int id;
    char name[TORIRS_NAME_MAX];
    char actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN];
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

struct ToriRS_Npctype
{
    int id;
    char name[TORIRS_NAME_MAX];
    char actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN];
    int combat_level;
    int size;
};

struct ToriRS_Objtype
{
    int id;
    char name[TORIRS_NAME_MAX];
    char inv_actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN];
    uint8_t stackable;
    int inventory_model_id;
    int zoom2d;
    int xan2d;
    int yan2d;
    int zan2d;
    int offset_x2d;
    int offset_y2d;
    int resize_x;
    int resize_y;
    int resize_z;
    int count_obj[10];
    int count_co[10];
    int ambient;
    int contrast;
    int* recolors_from;
    int* recolors_to;
    int recolor_count;
};

struct ToriRS_Sequence
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
    struct ToriRS_Animation* resolved;
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
struct ToriRS_SkeletalAnim
{
    int id;
    int bone_count;
    int frame_count;
    float* matrices; /* [frame_count * bone_count * 16] */
};

/** Per-vertex animaya skin data attached to a model. */
struct ToriRS_AnimayaSkin
{
    int vertex_count;
    uint8_t* group_counts; /* [vertex_count] */
    uint8_t** groups;      /* [vertex_count][group_count] bone indices  */
    uint8_t** scales;      /* [vertex_count][group_count] bone weights  */
};

#define TORIRS_SPRITE_REF_MAX 128
#define TORIRS_COMPONENT_TEXT_MAX 256
#define TORIRS_FONT_GLYPH_COUNT 94

struct ToriRS_SpriteFrame
{
    uint32_t* pixels_argb;
    int width;
    int height;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
};

struct ToriRS_Sprite
{
    char name[64];
    struct ToriRS_SpriteFrame* frames;
    int frame_count;
};

struct ToriRS_Font
{
    char name[16];
    uint8_t* glyph_alpha[TORIRS_FONT_GLYPH_COUNT];
    int glyph_width[TORIRS_FONT_GLYPH_COUNT];
    int glyph_height[TORIRS_FONT_GLYPH_COUNT];
    int offset_x[TORIRS_FONT_GLYPH_COUNT];
    int offset_y[TORIRS_FONT_GLYPH_COUNT];
    int advance[TORIRS_FONT_GLYPH_COUNT + 1];
    int draw_width[256];
    int line_height;
    char charcodeset[256];
};

enum ToriRS_ComponentType
{
    TORIRS_COMPONENT_LAYER = 0,
    TORIRS_COMPONENT_INV,
    TORIRS_COMPONENT_RECT,
    TORIRS_COMPONENT_TEXT,
    TORIRS_COMPONENT_GRAPHIC,
    TORIRS_COMPONENT_MODEL,
    TORIRS_COMPONENT_INV_TEXT,
    TORIRS_COMPONENT_LINE,
};

/* Some interface onLoad hooks (e.g. the bank's) declare 30+ int script params
 * (CS2_Script.int_argument_count), well past the old cap of 16. Truncating argv there
 * left the trailing script locals uninitialized (0), which surfaced as a CC_CREATE
 * with parent_id=0 several gosub calls deep. */
#define TORIRS_COMPONENT_HOOK_ARG_MAX 64
/** Matches OSRS INV slotGraphic array length (equipment silhouettes, etc.). */
#define TORIRS_INV_SLOT_MAX 20
#define TORIRS_INVENTORY_TRIGGER_MAX 8
#define TORIRS_VARP_TRIGGER_MAX 8

struct ToriRS_ScriptHook
{
    int argc;
    int argv[TORIRS_COMPONENT_HOOK_ARG_MAX];
};

struct ToriRS_Component
{
    int id;
    enum ToriRS_ComponentType type;
    int parent_id;
    int rel_x;
    int rel_y;
    int width;
    int height;
    /** Client.ts scrollHeight / dat1 scroll. Layer content height; 0 = no vertical scroll. */
    int scroll_height;
    /** dat2 scrollWidth. Layer content width; 0 = no horizontal scroll. */
    int scroll_width;
    /** IF3 layout base rect (dat2 baseX/baseY/baseWidth/baseHeight). */
    int base_x;
    int base_y;
    int base_width;
    int base_height;
    int8_t x_mode;
    int8_t y_mode;
    int8_t width_mode;
    int8_t height_mode;
    int aspect_w;
    int aspect_h;
    uint8_t if3;
    /** Raw cache graphic id (dat2 graphic); UITree scene_id when building. */
    int graphic;
    /** dat2 activeGraphic / spriteId2. */
    int graphic_active;
    /** dat2 outline (borderType). */
    int outline;
    /** dat2 graphicShadow. */
    int graphic_shadow;
    /** dat2 angle (spriteAngle, 16-bit scale in reference GL path). */
    int sprite_angle;
    uint8_t horizontal_flip;
    uint8_t vertical_flip;
    /** Raw cache inv-slot graphic ids (dat2 invSlotGraphicId). */
    int inv_slot_graphic_id[TORIRS_INV_SLOT_MAX];
    int transparency;
    /** Raw text horizontal alignment (dat2 textHorizontalAlignment). */
    int text_h_align;
    /** Raw text vertical alignment (dat2 textVerticalAlignment). */
    int text_v_align;
    /** Raw text line height (dat2 textLineHeight); 0 = font default. */
    int text_line_height;
    /** LINE widget direction: 1 = horizontal (dat2 lineDirection). */
    uint8_t line_horizontal;
    uint8_t drag_dead_zone;
    uint8_t drag_dead_time;
    char sprite_ref[TORIRS_SPRITE_REF_MAX];
    /** Client.ts graphic2 / dat activeGraphic: sprite when getIfActive. */
    char sprite_active_ref[TORIRS_SPRITE_REF_MAX];
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
    /** MODEL preview roll: dat2 modelZAngle (IF3 only). */
    int model_zan;
    /** MODEL orientation: dat2 modelXOffset / anInt5907 (IF3 only). Added to X after rotate. */
    int model_x_offset;
    /** MODEL depth offset: dat2 modelYOffset / anInt5921 (IF3 only). Added to Y and Z. */
    int model_y_offset;
    /** MODEL projection mode: dat2 modelOrthographic (IF3 only).
     *  1 = orthographic projection path; 0 = perspective (objRender / drawModel2D). */
    uint8_t model_orthog;
    /** MODEL projection scale: dat2 aBoolean411 (IF3 only).
     *  Deobfuscator Widget.useFixedZoom: 1 = zoom3d = widget zoom (drawModel2DAtZoom); 0 = 512
     * (drawModel2D). */
    uint8_t model_fixed_zoom;
    /** MODEL IF3 cache extras (dat2 aShort50 / aShort49 / anInt5957 / anInt5920). */
    int16_t model_cache_short50;
    int16_t model_cache_short49;
    int32_t model_cache_an5957;
    int32_t model_cache_an5920;
    int color;
    int filled;
    int font_id;
    int center;
    int shadowed;
    /** Client.ts text: inactive label. */
    char text[TORIRS_COMPONENT_TEXT_MAX];
    /** Client.ts text2 / dat activeText. Used when script comparators pass. */
    char active_text[TORIRS_COMPONENT_TEXT_MAX];
    int inv_cols;
    int inv_rows;
    int margin_x;
    int margin_y;
    /** LINE widget line thickness (dat2 lineWidth). */
    int line_width;
    /** Per-slot pixel offsets (dat1 invSlotOffsetX/Y). */
    int inv_slot_offset_x[TORIRS_INV_SLOT_MAX];
    int inv_slot_offset_y[TORIRS_INV_SLOT_MAX];
    /** Empty-slot background sprite refs (dat1 invSlotGraphic name or dat2 spr:id). */
    char inv_slot_sprite_ref[TORIRS_INV_SLOT_MAX][TORIRS_SPRITE_REF_MAX];
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
    struct ToriRS_ScriptHook on_load;
    struct ToriRS_ScriptHook on_click;
    struct ToriRS_ScriptHook on_varp_transmit;
    struct ToriRS_ScriptHook on_inv_transmit;
    int inventory_triggers_count;
    int inventory_triggers[TORIRS_INVENTORY_TRIGGER_MAX];
    int varp_triggers_count;
    int varp_triggers[TORIRS_VARP_TRIGGER_MAX];
    /** GRAPHIC with no sprite refs: draw nothing, keep layout hitbox. */
    uint8_t graphic_hitbox_only;
    /** dat2 tiled: repeat sprite across IF3 layout rect. */
    uint8_t tiled;
    char option[TORIRS_MENU_ACTION_LEN];
    char ops[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN];
};

struct ToriRS_ComponentPack
{
    struct ToriRS_Component* components;
    int component_count;
};

void
ToriRS_SpriteFrameFree(struct ToriRS_SpriteFrame* frame);

void
ToriRS_ComponentApplyWalkLayout(
    struct ToriRS_Component* component,
    int parent_id,
    int rel_x,
    int rel_y);

void
ToriRS_MapTerrainFree(struct ToriRS_MapTerrain* terrain);

void
ToriRS_MapLocsFree(struct ToriRS_MapLocs* locs);

void
ToriRS_FlotypeFree(struct ToriRS_Flotype* flotype);

void
ToriRS_LocationFree(struct ToriRS_Location* loc);

void
ToriRS_NpctypeFree(struct ToriRS_Npctype* npctype);

void
ToriRS_ObjtypeFree(struct ToriRS_Objtype* objtype);

void
ToriRS_SequenceFree(struct ToriRS_Sequence* seq);

void
ToriRS_ModelFree(struct ToriRS_Model* model);

/** Free all owned arrays on model; leaves an empty shell (counts/flags unchanged). */
void
ToriRS_ModelReleaseArrays(struct ToriRS_Model* model);

void
ToriRS_ModelAssertPnmTextureInvariant(struct ToriRS_Model const* model);

void
ToriRS_AnimationFree(struct ToriRS_Animation* anim);

void
ToriRS_SkeletalAnimFree(struct ToriRS_SkeletalAnim* skeletal);

void
ToriRS_AnimayaSkinFree(struct ToriRS_AnimayaSkin* skin);

void
ToriRS_TextureFree(struct ToriRS_Texture* texture);

void
ToriRS_SpriteFree(struct ToriRS_Sprite* sprite);

void
ToriRS_FontFree(struct ToriRS_Font* font);

void
ToriRS_ComponentFree(struct ToriRS_Component* component);

size_t
ToriRS_MapTerrainSizeOf(const struct ToriRS_MapTerrain* terrain);

size_t
ToriRS_MapLocsSizeOf(const struct ToriRS_MapLocs* locs);

size_t
ToriRS_FlotypeSizeOf(const struct ToriRS_Flotype* flotype);

size_t
ToriRS_LocationSizeOf(const struct ToriRS_Location* loc);

size_t
ToriRS_NpctypeSizeOf(const struct ToriRS_Npctype* npctype);

size_t
ToriRS_ObjtypeSizeOf(const struct ToriRS_Objtype* objtype);

size_t
ToriRS_SequenceSizeOf(const struct ToriRS_Sequence* seq);

size_t
ToriRS_ModelSizeOf(const struct ToriRS_Model* model);

size_t
ToriRS_AnimationSizeOf(const struct ToriRS_Animation* anim);

size_t
ToriRS_SkeletalAnimSizeOf(const struct ToriRS_SkeletalAnim* skeletal);

size_t
ToriRS_AnimayaSkinSizeOf(const struct ToriRS_AnimayaSkin* skin);

size_t
ToriRS_TextureSizeOf(const struct ToriRS_Texture* texture);

size_t
ToriRS_SpriteSizeOf(const struct ToriRS_Sprite* sprite);

size_t
ToriRS_FontSizeOf(const struct ToriRS_Font* font);

size_t
ToriRS_ComponentSizeOf(const struct ToriRS_Component* component);

#endif
