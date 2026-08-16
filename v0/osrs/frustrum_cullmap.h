#ifndef FRUSTRUM_CULLMAP_H
#define FRUSTRUM_CULLMAP_H

struct FrustrumCullmap
{
    int* cullmap;
    int radius;
    int near_clip_z; // Fixed-point multiplier for FOV (65536 = 90° FOV)
};

struct FrustrumCullmap*
frustrum_cullmap_new(
    int radius,
    int near_clip_z,
    int screen_width,
    int screen_height);
struct FrustrumCullmap*
frustrum_cullmap_new_nocull(int radius);

int
frustrum_cullmap_get(
    struct FrustrumCullmap* frustrum_cullmap,
    int x,
    int y,
    int pitch,
    int yaw);

void
frustrum_cullmap_free(struct FrustrumCullmap* frustrum_cullmap);

#endif