#ifndef TORIDRAW_ANIMATION_H
#define TORIDRAW_ANIMATION_H

#include <stdint.h>

struct ToriDraw_AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** bone_groups;
    uint16_t* bone_group_lengths;
};

struct ToriDraw_AnimFrame
{
    int id;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct ToriDraw_Animation
{
    struct ToriDraw_AnimBase* base;
    struct ToriDraw_AnimFrame* frames;
    int frame_count;
};

void
ToriDraw_AnimationFree(struct ToriDraw_Animation* anim);

/**
 * A baked skeletal (Animaya) animation.
 *
 * matrices[(frame * bone_count + bone) * 16 .. +15] is the column-major 4x4
 * float skinning matrix for that bone at that frame.
 */
struct ToriDraw_SkeletalAnim
{
    int    id;
    int    bone_count;
    int    frame_count;
    float* matrices; /* [frame_count * bone_count * 16] */
};

void
ToriDraw_SkeletalAnimFree(struct ToriDraw_SkeletalAnim* skeletal);

#endif
