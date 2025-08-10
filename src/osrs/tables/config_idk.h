#ifndef CONFIG_IDK_H
#define CONFIG_IDK_H

#include "cache.h"

#include <stdbool.h>

struct CacheConfigIdk
{
    int _id;

    int body_part_id;
    int* model_ids;
    int model_ids_count;

    int* recolors_from;
    int* recolors_to;
    int recolor_count;

    int* retextures_from;
    int* retextures_to;
    int retexture_count;

    bool is_not_selectable;

    int if_model_ids[10];
};

struct CacheConfigIdk* config_idk_new_decode(char* buffer, int buffer_size);
void config_idk_free(struct CacheConfigIdk* idk);

void config_idk_decode_inplace(struct CacheConfigIdk* idk, char* buffer, int buffer_size);

#endif