#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_IDK_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_IDK_H

#include "../rsbuffer.h"
#include "dat2_configs.h"

#include <stdbool.h>

struct RSCache_Dat2ConfigIdk
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

struct RSCache_Dat2ConfigIdk*
RSCache_Dat2ConfigIdkNewDecode(
    char* buffer,
    int buffer_size);
void
RSCache_Dat2ConfigIdkFree(struct RSCache_Dat2ConfigIdk* idk);

void
RSCache_Dat2ConfigIdkDecodeInplace(
    struct RSCache_Dat2ConfigIdk* idk,
    char* buffer,
    int buffer_size);

#endif