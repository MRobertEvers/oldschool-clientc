#ifndef SPRITES_H
#define SPRITES_H

#include <stdint.h>

#define FLAG_VERTICAL 1
#define FLAG_ALPHA 2

struct Sprite
{
    int id;
    int file_id;
    int frame;
    int max_width;
    int max_height;
    int width;
    int height;
    int offset_x;
    int offset_y;

    // Pixel data using the palette indexes.
    uint8_t* palette_pixels;
    uint8_t* pixel_alphas;
};

struct SpritePack
{
    int count;
    struct Sprite* sprites;

    int palette_length;
    int* palette;
};

struct Cache;
struct SpritePack* sprite_pack_new_decode(const unsigned char* data, int length);
void sprite_pack_free(struct SpritePack* pack);

int* sprite_get_pixels(struct Sprite* sprite, int* palette, int brightness);
int* sprite_texture_get_pixels(struct Sprite* sprite, int* palette, int size, int brightness);

#endif