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
    TORIRS_KIND_SPOTANIMTYPE,
    TORIRS_KIND_SOUND,
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
    /* Uniform priority for models that carry no per-face array (header priority != 255). Only
     * meaningful when merging: the merged model's faces inherit it, which is what keeps a loc's
     * parts layered (e.g. a statue drawn over the plinth it stands on). */
    uint8_t model_priority;
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
/* Examine description (reference LocType/NpcType/ObjType.desc, config opcode 3).
 * Sized well past the longest cache desc so the whole gjstr survives the copy. */
#define TORIRS_DESC_MAX 256
/* 64: matches UITREE_MENU_OPTION_LEN; col-tagged labels exceed 32 chars. */
#define TORIRS_MENU_ACTION_LEN 64

struct ToriRS_Location
{
    int id;
    char name[TORIRS_NAME_MAX];
    /** Examine text (LocType.desc, config opcode 3). Empty when the config
     *  carries none — the examine handler then falls back to "It's a <name>.". */
    char desc[TORIRS_DESC_MAX];
    char actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN];
    int* shapes;
    int** models;
    int* lengths;
    int shapes_and_model_count;
    int size_x;
    int size_z;
    int blocks_walk;
    int blocks_projectiles;
    /** LocType.forceapproach (config opcode 69): DirectionFlag bits (1 N, 2 E,
     *  4 S, 8 W) naming the sides this loc may NOT be interacted with from, in
     *  the loc's unrotated frame. 0 = any side. Consumed by the click-time
     *  approach test; the placed angle is applied at register time. */
    int force_approach;
    int wall_width;
    int seq_id;
    int contoured_ground;
    int contour_ground_type;
    int contour_ground_param;
    int sharelight;
    /** LocType.occlude (config opcode 23): walls/roofs with this set contribute
     *  to the planar occluder system (Client-TS mapo marks). Default 0. */
    int occlude;
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
    int map_function_id;
    int transform_varbit;
    int transform_varp;
    int* transforms;
    int transform_count;
    /** LocType.active: 0 = the reference never picks this loc (see
     *  torirs_location_from_rscache.c). */
    int is_interactive;
};

struct ToriRS_Npctype
{
    int id;
    char name[TORIRS_NAME_MAX];
    /** Examine text (NpcType.desc, config opcode 3). Empty on dat2 (OSRS) caches,
     *  where NPC examine is server-driven and no config opcode carries it. */
    char desc[TORIRS_DESC_MAX];
    char actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN];
    int combat_level;
    int size;
    int* models;
    int models_count;
    /** Chathead models (dat1 NpcType.heads / dat2 chathead_models). Merged and
     *  recoloured into the interface MODEL scene node when a dialogue sets an
     *  NPC head (reference NpcType.getHead). NULL/0 when the npc has no head. */
    int* heads;
    int heads_count;
    int* recolors_from;
    int* recolors_to;
    int recolor_count;
    int* retextures_from;
    int* retextures_to;
    int retexture_count;
    /* Movement animation set (dat1 config; -1 = none). This rev's NPC config
     * has no turnanim/runanim — turning falls back to walkanim. */
    int readyanim;
    int walkanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    /** NpcType.turnspeed (dat1 opcode 103, default 32). 0 = the entity never
     *  turns — Client-TS entityFace returns immediately for those. */
    int turn_speed;
    /** Model scale, 128 == 1.0 (reference widthScale/heightScale, dat1 resizeh/resizev,
     *  dat2 opcodes 97/98). Applied as scale(width, height, width) — NpcModelLoader. */
    int width_scale;
    int height_scale;
    /** NpcType.alwaysontop (opcode 99). Drives the draw-order tier: the
     *  reference adds alwaysontop NPCs before other players/normal NPCs
     *  (Client.ts addNpcs), so they win the one-entity-per-tile dedup. */
    bool alwaysontop;
    /** NpcType ambient/contrast (opcodes 100/101). Contrast arrives pre-scaled
     *  by 5 from the decoder. Used when the era/manifest enables
     *  npc_light_uses_type_ambient_contrast (xrsps); Client-TS ignores them. */
    int ambient;
    int contrast;
};

/* Spotanim (graphical effect) config — reference SpotType (config/SpotType.ts).
 * A single model animated by a seq, with recolour/retexture, resize and a
 * 90-degree angle, lit with custom ambient/contrast. Fed to the world as both
 * a free-standing MapSpotAnim (MAP_ANIM) and an entity-attached graphic. */
struct ToriRS_Spotanimtype
{
    int id;
    int model;   /* single model id */
    int seq;     /* animation seq id, or -1 */
    int resizeh; /* 128 == 1.0 */
    int resizev;
    int angle; /* 0 / 90 / 180 / 270 */
    int ambient;
    int contrast;
    int recol_s[6];
    int recol_d[6];
    int retex_s[6]; /* dat2 only; dat1 leaves 0 */
    int retex_d[6];
};

struct ToriRS_Idk
{
    int id;
    int body_part_id;
    int* model_ids;
    int model_ids_count;
    bool not_selectable;
    int recolors_from[10];
    int recolors_to[10];
    int heads[10];
};

struct ToriRS_Objtype
{
    int id;
    char name[TORIRS_NAME_MAX];
    /** Examine text (ObjType.desc, config opcode 3; dat2 raw field is `examine`).
     *  For bank notes it is synthesized in CacheProvider_ObjtypeGet (genCert). */
    char desc[TORIRS_DESC_MAX];
    char inv_actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN];
    /** Ground-stack ops (ObjType.op, config opcodes 30-34) — the right-click
     *  rows for an obj lying on a tile, distinct from the inventory ops. */
    char ground_actions[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN];
    uint8_t stackable;
    int inventory_model_id;
    /* Noted-item linkage (reference ObjType certlink/certtemplate, opcodes
     * 97/98; dat2 noted_id/noted_template). When cert_template >= 0 the item is
     * a bank note: its own model is 0 and the icon renders the cert_template
     * objtype's model (genCert), while cert_link names the base item. -1 = not
     * a note. */
    int cert_link;
    int cert_template;
    /** Weapon/equipment class from the cache record's `category` field, or 0.
     *  This is the key the combat interface's dbtable
     *  (`combat_interface_weapon_category`) is looked up by — cache.osrs230
     *  gives the bronze scimitar 21 and the abyssal whip 150. Carried through
     *  from the cache because nothing else can reconstruct it. */
    int category;
    /** Base GE/alch value (cache opcode 12). The loot tracker's value column
     *  is cost*qty; CS2 reads it through OC_COST (cs2_command 4003). */
    int cost;
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
    struct ToriRS_Param* params;
    int param_count;
    /* Worn-equipment model ids for the player appearance build (-1 = none). */
    int manwear;
    int manwear2;
    int manwear3;
    int womanwear;
    int womanwear2;
    int womanwear3;
    int manwear_offset_y;
    int womanwear_offset_y;
    /* Worn-equipment head model ids for the player head/chathead build
     * (reference ObjType.manhead/manhead2/womanhead/womanhead2; -1 = the item
     * covers no head, e.g. a full helm hides hair). */
    int manhead;
    int manhead2;
    int womanhead;
    int womanhead2;
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

/**
 * A sound effect, rendered.
 *
 * The cache stores sound effects as synthesiser programs, not recordings, so the
 * neutral type is what came out of running one: 8-bit unsigned mono PCM, which
 * is byte-for-byte a RIFF/WAVE payload and what every platform backend can take
 * with at most an integer conversion. 128 is silence.
 *
 * `loop_start`/`loop_end` are the effect's loop span in samples. The server's
 * SYNTH_SOUND carries a repeat count, so the span is kept rather than baked in:
 * one render is cached per id and the repeats are stamped in at play time
 * (ToriRS_SoundExpandLoops), which is what the reference client does too.
 *
 * `queue_delay` is the lead-in the render dropped, in client ticks. The server's
 * play delay is relative to the effect's own start, so the game adds this back
 * when queueing — otherwise an effect authored with a 600ms run-up plays 600ms
 * early.
 */
struct ToriRS_Sound
{
    int id;
    uint8_t* pcm;
    int sample_count;
    int sample_rate;
    int channels;
    int loop_start; /* -1 = the effect does not loop */
    int loop_end;
    int queue_delay;
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

struct ToriRS_Enum
{
    int id;
    bool output_is_string;
    int default_int;
    char* default_string;
    int* keys;
    int* int_values;
    char** string_values;
    int count;
};

struct ToriRS_Param
{
    int key;
    int int_value;
    char* string_value; /* NULL if int */
};

struct ToriRS_Struct
{
    int id;
    struct ToriRS_Param* params;
    int param_count;
};

struct ToriRS_ParamType
{
    int id;
    char type;
    int default_int;
    long long default_long;
    int is_string;
    char* default_string;
};

/** One rectangle of a world map area: a piece of the real world (source
 *  plane/region) blitted to a piece of the map surface (display region).
 *  The four cache section types differ only in which fields are present, so
 *  they decode into one struct — chunk granularity is 8 tiles, region 64. */
enum ToriRS_WorldMapSectionKind
{
    /* Cache type byte 0. Rectangle of regions -> rectangle of regions. */
    TORIRS_WORLDMAP_SECTION_REGION_RANGE = 0,
    /* Cache type byte 1. Single region -> single region. */
    TORIRS_WORLDMAP_SECTION_REGION = 1,
    /* Cache type byte 2. Chunk range inside one region, with a plane span. */
    TORIRS_WORLDMAP_SECTION_CHUNK_RANGE = 2,
    /* Cache type byte 3. Single chunk -> single chunk. */
    TORIRS_WORLDMAP_SECTION_CHUNK = 3,
};

struct ToriRS_WorldMapSection
{
    enum ToriRS_WorldMapSectionKind kind;
    int min_plane;
    int planes;
    /* Source (world) side. */
    int src_region_x;
    int src_region_y;
    int src_region_x_end;
    int src_region_y_end;
    int src_chunk_x;
    int src_chunk_y;
    int src_chunk_x_end;
    int src_chunk_y_end;
    /* Display (map surface) side. */
    int dst_region_x;
    int dst_region_y;
    int dst_region_x_end;
    int dst_region_y_end;
    int dst_chunk_x;
    int dst_chunk_y;
    int dst_chunk_x_end;
    int dst_chunk_y_end;
};

/** A map element placement from the "compositemap" archive. */
struct ToriRS_WorldMapIcon
{
    int element; /* map element (MEC) config id */
    int coord;   /* packed source coord: plane<<28 | x<<14 | y */
    int hidden;
};

/**
 * Where one piece of the map surface gets its tiles. `kind` 0 is a whole 64x64
 * region, `kind` 1 one 8x8 chunk of one — a region assembled from chunks has
 * several of these, all with the same dst_region_x/y.
 */
struct ToriRS_WorldMapRegionSource
{
    int kind;
    int min_plane;
    int planes;
    int src_region_x;
    int src_region_y;
    int src_chunk_x;
    int src_chunk_y;
    int dst_region_x;
    int dst_region_y;
    int dst_chunk_x;
    int dst_chunk_y;
    /* Geography table group/file. -1 at OSRS >= 238, which drops the pair. */
    int group_id;
    int file_id;
};

struct ToriRS_WorldMapArea
{
    int id;
    char* internal_name;
    char* external_name;
    int origin; /* packed coord */
    int background_colour;
    int is_main;
    int zoom;
    struct ToriRS_WorldMapSection* sections;
    int section_count;
    struct ToriRS_WorldMapIcon* icons;
    int icon_count;
    struct ToriRS_WorldMapRegionSource* region_sources;
    int region_source_count;
    /* Display-side region bounds, filled from the sections at decode time. */
    int region_low_x;
    int region_high_x;
    int region_low_y;
    int region_high_y;
};

/** Every world map area in the cache — one object, loaded once. */
struct ToriRS_WorldMapAreas
{
    struct ToriRS_WorldMapArea* areas;
    int count;
};

/** Map element config ("mapFunctions", config group 35). */
struct ToriRS_MapElement
{
    int id;
    char* name;
    int text_size;
    int category;
    int sprite_id;
};

enum ToriRS_ComponentType
{
    /* Wire values from IF1/IF3 (dat1/dat2) — must match cache decode. */
    TORIRS_COMPONENT_LAYER = 0,
    TORIRS_COMPONENT_UNUSED = 1,
    TORIRS_COMPONENT_INV = 2,
    TORIRS_COMPONENT_RECT = 3,
    TORIRS_COMPONENT_TEXT = 4,
    TORIRS_COMPONENT_GRAPHIC = 5,
    TORIRS_COMPONENT_MODEL = 6,
    TORIRS_COMPONENT_INV_TEXT = 7,
    TORIRS_COMPONENT_LINE = 9,
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

/** Hook string args (e.g. button labels passed to onLoad procs) are rare and
 * short — keep a small inline pool so hooks stay memcpy-safe PODs. */
#define TORIRS_COMPONENT_HOOK_STR_MAX 4
#define TORIRS_COMPONENT_HOOK_STR_LEN 80

struct ToriRS_ScriptHook
{
    int argc;
    int argv[TORIRS_COMPONENT_HOOK_ARG_MAX];
    /* Bit i set = arg position i is a string; strings fill strv[] in position
     * order (k-th set bit -> strv[k]). argv[i] is unused at string positions. */
    uint64_t str_mask;
    int str_argc;
    char strv[TORIRS_COMPONENT_HOOK_STR_MAX][TORIRS_COMPONENT_HOOK_STR_LEN];
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
    /** dat2 noClickThrough — a LAYER-only field, decoded beside scrollWidth /
     *  scrollHeight. The reference treats it as "input stops here": a layer
     *  carrying it swallows whatever is drawn underneath it (rt4
     *  `Cs1ScriptRunner:542` resets the minimenu to Cancel; `InterfaceList:666`
     *  unlinks the pending wheel dispatches). See UITreeComponent
     *  no_click_through, which until now only CS2's if_/cc_setnoclickthrough
     *  could set. */
    uint8_t no_click_through;
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
    /** Client.ts model2Type/model2Id: the model drawn when getIfActive() holds,
     *  the MODEL-widget twin of sprite_active_ref / active_text. Type 0 means
     *  "no model", and the reference then draws *nothing* — that is how the 254
     *  special-attack bar works: dark cover segments over a green bar, each
     *  cover going away once spec energy passes its threshold. */
    int active_model_type;
    int active_model_id;
    /** MODEL cache sequence (dat2 modelSeqId); -1 = none. Reference widget.sequenceId. */
    int model_seq_id;
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
    /** Inventory drag/swap allowed (reference IfType.objSwap || objReplace,
     * dat1 draggable || swappable). Equipment/worn grids decode false so their
     * items can't be dragged; the backpack decodes true. 1 = draggable. */
    int inv_can_drag;
    /** Inventory ObjType-op rows allowed (reference IfType.objOps, dat1
     * `interactable`). When false the item's Drop / wield / op1-5 rows are
     * suppressed — e.g. a shop's sell grid shows only its own iop buttons
     * (Value/Sell) + Examine. 1 = show obj ops. */
    int inv_obj_ops;
    /** Inventory "Use" row allowed (reference IfType.objUse, dat1 `usable`).
     * Suppressed on grids like the shop sell inventory. 1 = show "Use". */
    int inv_obj_use;
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
    /** IF1/IF3 clickMask / serverActiveProperties.events. */
    int32_t click_mask;
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
    /** Sized independently of scripts_count in the cache format. */
    int comparator_count;
    int* script_comparator;
    int* script_operand;
    uint8_t script_kind;
    struct ToriRS_ScriptHook on_load;
    struct ToriRS_ScriptHook on_click;
    struct ToriRS_ScriptHook on_op;
    struct ToriRS_ScriptHook on_mouse_over;
    struct ToriRS_ScriptHook on_mouse_leave;
    struct ToriRS_ScriptHook on_drag;
    struct ToriRS_ScriptHook on_drag_complete;
    /* These four are dispatched at runtime but used to be dropped here, so a
     * cache-authored handler could never fire -- only a CS2 SETON* call could
     * populate them. See uitree_interact.c (hold/repeat/wheel) and app.c (timer). */
    struct ToriRS_ScriptHook on_hold;
    struct ToriRS_ScriptHook on_mouse_repeat;
    struct ToriRS_ScriptHook on_scroll_wheel;
    struct ToriRS_ScriptHook on_timer;
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
    /** BUTTON_TARGET spell strings (dat1 targetVerb/targetText, dat2
     * targetVerb/targetText): "Cast" + "Wind Strike" form the "Cast Wind
     * Strike ..." target-mode verb. Empty when the component is not a spell. */
    char target_verb[TORIRS_MENU_ACTION_LEN];
    char target_base[TORIRS_MENU_ACTION_LEN];
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
ToriRS_IdkFree(struct ToriRS_Idk* idk);

void
ToriRS_ObjtypeFree(struct ToriRS_Objtype* objtype);

void
ToriRS_SpotanimtypeFree(struct ToriRS_Spotanimtype* spotanimtype);

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
ToriRS_SoundFree(struct ToriRS_Sound* sound);

void
ToriRS_EnumFree(struct ToriRS_Enum* e);

void
ToriRS_StructFree(struct ToriRS_Struct* s);

void
ToriRS_ParamTypeFree(struct ToriRS_ParamType* p);

void
ToriRS_ComponentFree(struct ToriRS_Component* component);

void
ToriRS_ComponentPackFree(struct ToriRS_ComponentPack* pack);

size_t
ToriRS_ComponentPackSizeOf(const struct ToriRS_ComponentPack* pack);

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
ToriRS_IdkSizeOf(const struct ToriRS_Idk* idk);

size_t
ToriRS_ObjtypeSizeOf(const struct ToriRS_Objtype* objtype);

size_t
ToriRS_SpotanimtypeSizeOf(const struct ToriRS_Spotanimtype* spotanimtype);

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
ToriRS_SoundSizeOf(const struct ToriRS_Sound* sound);

size_t
ToriRS_ComponentSizeOf(const struct ToriRS_Component* component);

#endif
