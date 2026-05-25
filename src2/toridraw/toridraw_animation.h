#ifndef TORIDRAW_ANIMATION_H
#define TORIDRAW_ANIMATION_H

#include <stdint.h>

struct CacheDatAnimBaseFrames;

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

struct ToriDraw_Animation*
toridraw_animation_new_from_cache_dat_animbaseframes(struct CacheDatAnimBaseFrames* abf);

void
toridraw_animation_free(struct ToriDraw_Animation* anim);

#endif
