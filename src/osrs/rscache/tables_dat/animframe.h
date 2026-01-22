#ifndef TABLES_DAT_ANIMFRAME_H
#define TABLES_DAT_ANIMFRAME_H

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
    int* groups;
    int* x;
    int* y;
    int* z;
    int delay;
};

struct CacheDatAnimBaseFrames
{
    struct CacheAnimBase* base;

    struct CacheAnimframe* frames;
    int frame_count;
};

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

#endif // TABLES_DAT_ANIMFRAME_H