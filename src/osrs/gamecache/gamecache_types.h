#ifndef GAMECACHE_TYPES_H
#define GAMECACHE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/** Map chunk dimensions (aligned with OSRS map square layout). */
#define GAMECACHE_MAP_TERRAIN_X 64
#define GAMECACHE_MAP_TERRAIN_Z 64
#define GAMECACHE_MAP_TERRAIN_LEVELS 4

#define GAMECACHE_MAP_TILE_COUNT                                                                  \
    (GAMECACHE_MAP_TERRAIN_X * GAMECACHE_MAP_TERRAIN_Z * GAMECACHE_MAP_TERRAIN_LEVELS)

#define GAMECACHE_MAPREGIONXZ(x, z) ((x) << 8 | (z))

static inline int
gamecache_map_tile_index(
    int x,
    int z,
    int level)
{
    return x + z * GAMECACHE_MAP_TERRAIN_X + level * (GAMECACHE_MAP_TERRAIN_X * GAMECACHE_MAP_TERRAIN_Z);
}

/** Single terrain cell (layout matches historical client map floor decode). */
struct GameCacheFloor
{
    uint16_t overlay_id;
    uint8_t underlay_id;
    int16_t height;
    uint8_t attr_opcode;
    uint8_t settings;
    uint8_t shape;
    uint8_t rotation;
};

/** One map square of terrain. */
struct GameCacheTerrain
{
    bool _is_fixedup;
    int map_x;
    int map_z;
    struct GameCacheFloor tiles_xyz[GAMECACHE_MAP_TILE_COUNT];
};

/** Floor paint / texture descriptor (underlay uses the same fields as overlay in this pipeline). */
struct GameCacheFloorType
{
    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;
    bool flotype_overlay;
    char* flotype_name;
    int _id;
};

/** One scenery instance on a map chunk. */
struct GameCacheLoc
{
    int loc_id;
    int shape_select;
    int orientation;
    int chunk_pos_x;
    int chunk_pos_z;
    int chunk_pos_level;
};

/** All scenery instances for one map square. */
struct GameCacheLocList
{
    int _chunk_mapx;
    int _chunk_mapz;
    struct GameCacheLoc* locs;
    int locs_count;
};

/** Key/value bag for loc config params (same layout as RS Params). */
struct GameCacheParams
{
    int* keys;
    void** values;
    bool* is_string;
    int count;
    int capacity;
};

/** Full loc type definition used by world/scene build (field order matches decoded RS loc config). */
struct GameCacheLocConfig
{
    int _id;
    int* shapes;
    int** models;
    int* lengths;
    int shapes_and_model_count;
    char* name;
    char* desc;
    int size_x;
    int size_z;
    int blocks_walk;
    int blocks_projectiles;
    int wall_width;
    int is_interactive;
    int contoured_ground;
    int contour_ground_type;
    int contour_ground_param;
    int sharelight;
    int occlude;
    int seq_id;
    int ambient;
    int contrast;
    char* actions[10];
    int* recolors_from;
    int* recolors_to;
    int recolor_count;
    int* retextures_from;
    int* retextures_to;
    int retexture_count;
    int map_function_id;
    int mirrored;
    int shadowed;
    int resize_x;
    int resize_height;
    int resize_z;
    int map_scene_id;
    int offset_x;
    int offset_y;
    int offset_z;
    int obstructs_ground;
    int break_routefinding;
    int support_items;
    int transform_varbit;
    int transform_varp;
    int* transforms;
    int transform_count;
    int ambient_sound_id;
    int ambient_sound_distance;
    int ambient_sound_retain;
    int ambient_sound_ticks_min;
    int ambient_sound_ticks_max;
    int* ambient_sound_ids;
    int ambient_sound_id_count;
    bool seq_random_start;
    int* random_seq_ids;
    int* random_seq_delays;
    int random_seq_id_count;
    int* campaign_ids;
    int campaign_id_count;
    struct GameCacheParams param_values;
};

/** Triangle mesh (layout matches RS model decode used by the renderer path). */
struct GameCacheModel
{
    int _id;
    int _model_type;
    int _flags;
    int _ids[10];
    int vertex_count;
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;
    uint8_t* vertex_bone_map;
    uint8_t* face_bone_map;
    int face_count;
    int* face_indices_a;
    int* face_indices_b;
    int* face_indices_c;
    uint8_t* face_alphas;
    uint8_t* face_infos;
    uint8_t* face_priorities;
    uint16_t* face_colors;
    uint8_t model_priority;
    int textured_face_count;
    uint16_t* textured_p_coordinate;
    uint16_t* textured_m_coordinate;
    uint16_t* textured_n_coordinate;
    int16_t* face_textures;
    int16_t* face_texture_coords;
    unsigned char* textureRenderTypes;
    int rotated;
};

/** Runtime sequence (frame ids + timing) for scene animation. */
struct GameCacheSequence
{
    int frame_count;
    int* frames;
    int* iframes;
    int* delay;
    int loops;
    int* walkmerge;
    bool stretches;
    int priority;
    int replaceheldleft;
    int replaceheldright;
    int maxloops;
    int preanim_move;
    int postanim_move;
    int duplicate_behavior;
};

struct GameCacheAnimBase
{
    int length;
    uint8_t* types;
    uint8_t** labels;
    uint16_t* label_counts;
};

struct GameCacheAnimframe
{
    int id;
    struct GameCacheAnimBase* base;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct GameCacheObj
{
    int model;
    char* name;
    char* desc;
    int* recol_s;
    int* recol_d;
    int recol_count;
    int zoom2d;
    int xan2d;
    int yan2d;
    int zan2d;
    int xof2d;
    int yof2d;
    bool code9;
    int code10;
    bool stackable;
    int cost;
    bool members;
    int manwearOffsetY;
    int womanwearOffsetY;
    int manwear;
    int manwear2;
    int womanwear;
    int womanwear2;
    int manwear3;
    int womanwear3;
    int manhead;
    int manhead2;
    int womanhead;
    int womanhead2;
    int certlink;
    int certtemplate;
    int resizex;
    int resizey;
    int resizez;
    int ambient;
    int contrast;
    int* countobj;
    int* countco;
    int countobj_count;
    char* op[5];
    char* iop[5];
};

struct GameCacheIdk
{
    int type;
    int* models;
    int models_count;
    int recol_s[10];
    int recol_d[10];
    int heads[10];
    bool disable;
};

struct GameCacheNpc
{
    char* name;
    char* desc;
    int size;
    int* models;
    int models_count;
    int* heads;
    int heads_count;
    int readyanim;
    int walkanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    bool animHasAlpha;
    int* recol_s;
    int* recol_d;
    int recol_count;
    char* op[5];
    int resizex;
    int resizey;
    int resizez;
    bool minimap;
    int vislevel;
    int resizeh;
    int resizev;
    bool alwaysontop;
    int headicon;
    int ambient;
    int contrast;
};

struct GameCacheSpotAnim
{
    int model;
    int seq_id;
    int anim_gfx_height;
    int resizeh;
    int resizev;
    int orientation;
    int ambient;
    int contrast;
};

#endif
