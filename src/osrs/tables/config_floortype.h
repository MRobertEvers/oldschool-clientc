#ifndef CONFIG_FLOORTYPE_H
#define CONFIG_FLOORTYPE_H

#include <stdbool.h>
#include <stdint.h>

struct Underlay
{
    int rgb_color;
};

struct CacheOverlay
{
    int rgb_color;
    int texture;
    int secondary_rgb_color;
    bool hide_underlay;
};

// void config_floortype_overlay_init(struct CacheOverlay* overlay);
// void config_floortype_underlay_init(struct Underlay* underlay);

struct CacheOverlay* config_floortype_overlay_new_decode(char* buffer, int buffer_size);
void config_floortype_overlay_decode_inplace(
    struct CacheOverlay* overlay, char* buffer, int buffer_size);
void config_floortype_overlay_free(struct CacheOverlay* overlay);

struct Underlay* config_floortype_underlay_new_decode(char* buffer, int buffer_size);
void
config_floortype_underlay_decode_inplace(struct Underlay* underlay, char* buffer, int buffer_size);

void config_floortype_underlay_free(struct Underlay* underlay);

#endif