#ifndef CONFIG_SPOTANIM_H
#define CONFIG_SPOTANIM_H

struct CacheDatConfigSpotAnim
{
    int model;  /* model ID for this spot animation */
    int seq_id; /* sequence ID (-1 if none) */
    int anim_gfx_height; /* height above tile centre, scaled */
    int resizeh;
    int resizev;
    int orientation;
    int ambient;
    int contrast;
};

/** Decode a single spotanim config from a raw data buffer. Caller owns result. */
struct CacheDatConfigSpotAnim*
cache_dat_config_spotanim_decode_one(
    void* data,
    int size);

void
cache_dat_config_spotanim_free(struct CacheDatConfigSpotAnim* s);

#endif
