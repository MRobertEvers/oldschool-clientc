#ifndef RSCACHE_DATATYPES_DAT2_CONFIG_OBJ_H
#define RSCACHE_DATATYPES_DAT2_CONFIG_OBJ_H

#include "../rsbuffer.h"
#include "dat2_configs.h"

#include <stdbool.h>

struct RSCache_Dat2ConfigObj
{
    int _id;

    // Null terminated strings
    char* name;
    char* examine;

    int resize_x;
    int resize_y;
    int resize_z;

    int xan2d;
    int yan2d;
    int zan2d;

    // Alchemy value
    int cost;
    bool tradeable;

    int stacking_behaviour;
    // Also the ground model id.
    int inventory_model_id;
    int wearpos_1;
    int wearpos_2;
    int wearpos_3;

    bool is_members;

    int* recolors_from;
    int* recolors_to;
    int recolor_count;

    int* retextures_from;
    int* retextures_to;
    int retexture_count;

    int zoom2d;
    int offset_x2d;
    int offset_y2d;

    int ambient;
    int contrast;

    // ??
    int count_co[10];
    int count_obj[10];

    char* actions[5];
    char** sub_actions[5];
    char* if_actions[5];

    // 	public int maleModel0 = -1;
    // public int maleModel1 = -1;
    // public int maleModel2 = -1;
    // public int maleOffset;
    // public int maleHeadModel = -1;
    // public int maleHeadModel2 = -1;

    // public int femaleModel0 = -1;
    // public int femaleModel1 = -1;
    // public int femaleModel2 = -1;
    // public int femaleOffset;
    // public int femaleHeadModel = -1;
    // public int femaleHeadModel2 = -1;

    int male_model_0;
    int male_model_1;
    int male_model_2;
    int male_offset;
    int male_head_model;
    int male_head_model_2;

    int female_model_0;
    int female_model_1;
    int female_model_2;
    int female_offset;
    int female_head_model;
    int female_head_model_2;

    // public int category;

    // public int notedID = -1;
    // public int notedTemplate = -1;

    // public int team;
    // public int weight;

    // public int shiftClickDropIndex = -2;

    // public int boughtId = -1;
    // public int boughtTemplateId = -1;

    // public int placeholderId = -1;
    // public int placeholderTemplateId = -1;

    int category;
    int noted_id;
    int noted_template;
    int team;
    int weight;
    int shift_click_drop_index;
    int bought_id;
    int bought_template_id;

    int placeholder_id;
    int placeholder_template_id;

    struct RSCache_Params params;
};

/**
 * Encode an obj (item) record.
 *
 * Omits fields still at their decode default and writes opcodes in ascending
 * order, which matches how the packer emits them.
 *
 * Three fields cannot be reproduced, so records using them round-trip
 * semantically but not byte-exactly:
 *   - opcode 9, a string the decoder discards;
 *   - an action of the literal "Hidden", which the decoder normalises to NULL and
 *     is therefore indistinguishable from an absent action;
 *   - a name of the literal "null", indistinguishable from an absent opcode 2.
 */
uint32_t
RSCache_Dat2ConfigObjEncode(
    const struct RSCache_Dat2ConfigObj* object,
    uint8_t* out,
    uint32_t out_capacity);

struct RSCache_Dat2ConfigObj*
RSCache_Dat2ConfigObjNewDecode(
    char* buffer,
    int buffer_size);
void
RSCache_Dat2ConfigObjFree(struct RSCache_Dat2ConfigObj* object);

void
RSCache_Dat2ConfigObjDecodeInplace(
    struct RSCache_Dat2ConfigObj* object,
    char* buffer,
    int buffer_size);

#endif