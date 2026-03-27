#ifndef TABLES_DAT_ANIMFRAME_H
#define TABLES_DAT_ANIMFRAME_H

#include <stdint.h>

struct CacheAnimBase
{
    int length;
    int* types;
    int** labels;
    int* label_counts;
};

struct CacheAnimframe
{
    int id;
    struct CacheAnimBase* base;
    int length;
    int16_t* groups;
    int16_t* x;
    int16_t* y;
    int16_t* z;
    int delay;
};

struct CacheDatAnimBaseFrames
{
    struct CacheAnimBase* base;

    struct CacheAnimframe* frames;
    int frame_count;
};

/**
 * data := archive from the anim table
 */
struct CacheDatAnimBaseFrames*
cache_dat_animbaseframes_new_decode(
    char* data,
    int data_size);

struct CacheAnimBase*
cache_dat_animbase_new_decode(
    char* data,
    int data_size);

void
cache_dat_animframe_free(struct CacheAnimframe* animframe);

void
cache_dat_animframe_free_inplace(struct CacheAnimframe* frame);

void
cache_dat_animbaseframes_free(struct CacheDatAnimBaseFrames* abf);

#endif // TABLES_DAT_ANIMFRAME_H