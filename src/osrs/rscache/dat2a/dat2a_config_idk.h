#ifndef RSCACHE_RSCACHEDAT2A_CONFIGIDK_H
#define RSCACHE_RSCACHEDAT2A_CONFIGIDK_H

#include "../dat2disk/dat2disk.h"
#include "../shared/shared_file_list.h"
#include "dat2a_configs.h"

#include <stdbool.h>

struct RSCacheDat2A_ConfigIdk
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

struct RSCacheDat2A_ConfigIdk*
RSCacheDat2A_ConfigIdkNewDecode(
    char* buffer,
    int buffer_size);
void
RSCacheDat2A_ConfigIdkFree(struct RSCacheDat2A_ConfigIdk* idk);

void
RSCacheDat2A_ConfigIdkDecodeInplace(
    struct RSCacheDat2A_ConfigIdk* idk,
    char* buffer,
    int buffer_size);

#endif