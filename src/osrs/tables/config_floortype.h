#ifndef CONFIG_FLOORTYPE_H
#define CONFIG_FLOORTYPE_H

#include <stdbool.h>
#include <stdint.h>

struct CacheConfigUnderlay
{
    int rgb_color;
};

struct CacheConfigOverlay
{
    int _id;
    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;
};

// void config_floortype_overlay_init(struct CacheConfigOverlay* overlay);
// void config_floortype_underlay_init(struct CacheConfigUnderlay* underlay);

struct CacheConfigOverlay*
config_floortype_overlay_new_decode(
    char* buffer,
    int buffer_size);
void
config_floortype_overlay_decode_inplace(
    struct CacheConfigOverlay* overlay,
    char* buffer,
    int buffer_size);
void
config_floortype_overlay_free(struct CacheConfigOverlay* overlay);
void
config_floortype_overlay_free_inplace(struct CacheConfigOverlay* overlay);

struct CacheConfigUnderlay*
config_floortype_underlay_new_decode(
    char* buffer,
    int buffer_size);
void
config_floortype_underlay_decode_inplace(
    struct CacheConfigUnderlay* underlay,
    char* buffer,
    int buffer_size);

void
config_floortype_underlay_free(struct CacheConfigUnderlay* underlay);
void
config_floortype_underlay_free_inplace(struct CacheConfigUnderlay* underlay);

#endif