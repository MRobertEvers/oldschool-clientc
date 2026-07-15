#ifndef RSCACHE_RSCACHEDAT1A_CONFIGSPOTANIM_H
#define RSCACHE_RSCACHEDAT1A_CONFIGSPOTANIM_H

struct RSCacheDat1A_ConfigSpotanim
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
struct RSCacheDat1A_ConfigSpotanim*
RSCacheDat1A_ConfigSpotanimDecodeOne(
    void* data,
    int size);

void
RSCacheDat1A_ConfigSpotanimFree(struct RSCacheDat1A_ConfigSpotanim* s);

#endif
