#ifndef RSCACHE_RSCACHEDAT1A_ANIMFRAME_H
#define RSCACHE_RSCACHEDAT1A_ANIMFRAME_H

#include <stdint.h>

struct RSCacheDat1A_AnimBase
{
    int length;
    uint8_t* types;
    uint8_t** labels;
    uint16_t* label_counts;
};

struct RSCacheDat1A_AnimFrame
{
    int id;
    struct RSCacheDat1A_AnimBase* base;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct RSCacheDat1A_AnimBaseFrames
{
    struct RSCacheDat1A_AnimBase* base;

    struct RSCacheDat1A_AnimFrame* frames;
    int frame_count;
};

/**
 * data := archive from the anim table
 */
struct RSCacheDat1A_AnimBaseFrames*
RSCacheDat1A_AnimBaseFramesNewDecode(
    char* data,
    int data_size);

struct RSCacheDat1A_AnimBase*
RSCacheDat1A_AnimBaseNewDecode(
    char* data,
    int data_size);

void
RSCacheDat1A_AnimFrameFree(struct RSCacheDat1A_AnimFrame* animframe);

void
RSCacheDat1A_AnimFrameFreeInplace(struct RSCacheDat1A_AnimFrame* frame);

void
RSCacheDat1A_AnimBaseFramesFree(struct RSCacheDat1A_AnimBaseFrames* abf);

#endif // TABLES_DAT_ANIMFRAME_H