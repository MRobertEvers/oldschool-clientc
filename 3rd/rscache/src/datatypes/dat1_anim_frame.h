#ifndef RSCACHE_DATATYPES_DAT1_ANIM_FRAME_H
#define RSCACHE_DATATYPES_DAT1_ANIM_FRAME_H

#include <stdint.h>

struct RSCache_Dat1AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** labels;
    uint16_t* label_counts;
};

struct RSCache_Dat1AnimFrame
{
    int id;
    struct RSCache_Dat1AnimBase* base;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct RSCache_Dat1AnimBaseFrames
{
    struct RSCache_Dat1AnimBase* base;

    struct RSCache_Dat1AnimFrame* frames;
    int frame_count;
};

/**
 * data := archive from the anim table
 */
struct RSCache_Dat1AnimBaseFrames*
RSCache_Dat1AnimBaseFramesNewDecode(
    char* data,
    int data_size);

struct RSCache_Dat1AnimBase*
RSCache_Dat1AnimBaseNewDecode(
    char* data,
    int data_size);

void
RSCache_Dat1AnimFrameFree(struct RSCache_Dat1AnimFrame* animframe);

void
RSCache_Dat1AnimFrameFreeInplace(struct RSCache_Dat1AnimFrame* frame);

void
RSCache_Dat1AnimBaseFramesFree(struct RSCache_Dat1AnimBaseFrames* abf);

#endif // RSCACHE_DATATYPES_DAT1_ANIM_FRAME_H
