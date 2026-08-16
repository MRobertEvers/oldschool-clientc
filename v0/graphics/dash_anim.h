#ifndef DASH_ANIM_H
#define DASH_ANIM_H

#include <stdbool.h>
#include <stdint.h>

struct DashFrame
{
    int _id;
    int framemap_id;
    int translator_count;
    int16_t* index_frame_ids;
    int16_t* translator_arg_x;
    int16_t* translator_arg_y;
    int16_t* translator_arg_z;
    bool showing;
};

struct DashFramemap
{
    int id;
    uint8_t* types;
    uint8_t** bone_groups;
    uint16_t* bone_groups_lengths;
    int length;
};

void
dashRSCacheDat2A_FrameFree(struct DashFrame* frame);

void
dashRSCacheDat2A_FramemapFree(struct DashFramemap* framemap);

#endif