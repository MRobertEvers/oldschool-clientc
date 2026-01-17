#ifndef OSRS_NPC_TYPE_H
#define OSRS_NPC_TYPE_H

#include <stdbool.h>

/**
 * Sourced from Runelite!
 *
 */
struct CacheConfigNPCType
{
    int* models;
    int models_count;
    char* name;
    int size;
    int standing_animation;
    int walking_animation;
    int idle_rotate_left_animation;
    int idle_rotate_right_animation;
    int rotate180_animation;
    int rotate_left_animation;
    int rotate_right_animation;
    char* actions[5]; // Options 30-34
    short* recolor_to_find;
    short* recolor_to_replace;
    int recolor_count;
    short* retexture_to_find;
    short* retexture_to_replace;
    int retexture_count;
    int* chathead_models;
    int chathead_models_count;
    bool is_minimap_visible;
    int combat_level;
    int width_scale;
    int height_scale;
    bool has_render_priority;
    int ambient;
    int contrast;
    int* head_icon_archive_ids;
    short* head_icon_sprite_index;
    int head_icon_count;
    int rotation_speed;
    int varbit_id;
    int varp_index;
    int* configs;
    int configs_count;
    bool is_interactable;
    bool rotation_flag;
    bool is_pet;
    int run_animation;
    int run_rotate180_animation;
    int run_rotate_left_animation;
    int run_rotate_right_animation;
    int crawl_animation;
    int crawl_rotate180_animation;
    int crawl_rotate_left_animation;
    int crawl_rotate_right_animation;
    bool low_priority_follower_ops;
    int height;
    int category;
    int stats[6]; // Stats for opcodes 74-79
    // HashMap equivalent for params
    struct
    {
        int* keys;
        void** values;
        bool* is_string;
        int count;
        int capacity;
    } params;
};

struct CacheConfigNPCType* config_npctype_new_decode(int revision, char* buffer, int buffer_size);
void config_npctype_free(struct CacheConfigNPCType* npc);

void print_npc_type(const struct CacheConfigNPCType* npc);
void free_npc_type(struct CacheConfigNPCType* npc);

#endif