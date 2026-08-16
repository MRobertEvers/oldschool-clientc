#include "test_sprites.h"

#include <stdlib.h>
#include <string.h>

static uint32_t
argb(int a, int r, int g, int b)
{
    return (uint32_t)((a << 24) | (r << 16) | (g << 8) | b);
}

static struct ToriDraw_Sprite*
make_sideicon_sprite(int tabno)
{
    int const w = TEST_ICON_SIZE;
    int const h = TEST_ICON_SIZE;
    uint32_t* pixels = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if( !pixels )
        return NULL;

    int const hue = (tabno * 37) % 256;
    uint32_t fill = argb(255, 40 + hue, 80 + ((hue * 2) % 120), 120 + ((hue * 3) % 100));
    for( int i = 0; i < w * h; i++ )
        pixels[i] = fill;

    /* Tab number label stripe at top. */
    for( int x = 4; x < w - 4; x++ )
        pixels[x] = argb(255, 255, 255, 255);

    if( tabno == 4 )
    {
        /* Equipment: orange cross pattern. */
        uint32_t cross = argb(255, 255, 140, 0);
        for( int y = 6; y < h - 6; y++ )
        {
            pixels[y * w + (w / 2)] = cross;
            pixels[(h / 2) * w + y] = cross;
        }
        for( int x = 2; x < w - 2; x++ )
            pixels[(h / 2) * w + x] = cross;
    }

    return ToriDraw_SpriteNewFromArgbOwned(pixels, w, h);
}

static struct ToriDraw_Sprite*
make_redstone_sprite(int variant)
{
    int const w = 28;
    int const h = 28;
    uint32_t* pixels = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if( !pixels )
        return NULL;

    uint32_t highlight = argb(220, 255, 60, 60);
    uint32_t edge = argb(255, 200, 20, 20);
    switch( variant % 3 )
    {
    case 1:
        highlight = argb(220, 255, 200, 40);
        edge = argb(255, 220, 160, 0);
        break;
    case 2:
        highlight = argb(220, 120, 220, 255);
        edge = argb(255, 40, 120, 220);
        break;
    default:
        break;
    }

    for( int y = 2; y < h - 2; y++ )
    {
        for( int x = 2; x < w - 2; x++ )
        {
            int on_edge = (x == 2 || x == w - 3 || y == 2 || y == h - 3);
            pixels[y * w + x] = on_edge ? edge : highlight;
        }
    }

    if( variant >= TEST_REDSTONE_2H )
    {
        for( int x = 4; x < w - 4; x++ )
            pixels[(h / 2) * w + x] = argb(255, 255, 255, 255);
    }

    return ToriDraw_SpriteNewFromArgbOwned(pixels, w, h);
}

static bool
make_atlas(
    struct TestSpriteAtlas* atlas,
    int count,
    struct ToriDraw_Sprite* (*factory)(int index))
{
    atlas->sprites = calloc((size_t)count, sizeof(struct ToriDraw_Sprite*));
    if( !atlas->sprites )
        return false;
    atlas->count = count;

    for( int i = 0; i < count; i++ )
    {
        atlas->sprites[i] = factory(i);
        if( !atlas->sprites[i] )
        {
            for( int j = 0; j < i; j++ )
            {
                free(atlas->sprites[j]->pixels_argb);
                free(atlas->sprites[j]);
            }
            free(atlas->sprites);
            atlas->sprites = NULL;
            atlas->count = 0;
            return false;
        }
    }
    return true;
}

bool
test_sprites_create(struct TestSpriteSet* out)
{
    if( !out )
        return false;
    memset(out, 0, sizeof(*out));

    if( !make_atlas(&out->sideicons, TEST_SIDEICON_COUNT, make_sideicon_sprite) )
        return false;
    if( !make_atlas(&out->redstone, TEST_REDSTONE_ATLAS_COUNT, make_redstone_sprite) )
    {
        test_sprites_free(out);
        return false;
    }
    return true;
}

void
test_sprites_free(struct TestSpriteSet* set)
{
    if( !set )
        return;

    if( set->sideicons.sprites )
    {
        for( int i = 0; i < set->sideicons.count; i++ )
        {
            free(set->sideicons.sprites[i]->pixels_argb);
            free(set->sideicons.sprites[i]);
        }
        free(set->sideicons.sprites);
        set->sideicons.sprites = NULL;
        set->sideicons.count = 0;
    }

    if( set->redstone.sprites )
    {
        for( int i = 0; i < set->redstone.count; i++ )
        {
            free(set->redstone.sprites[i]->pixels_argb);
            free(set->redstone.sprites[i]);
        }
        free(set->redstone.sprites);
        set->redstone.sprites = NULL;
        set->redstone.count = 0;
    }
}

struct ToriDraw_Sprite*
test_sprites_get(
    struct TestSpriteSet const* set,
    int scene_id,
    int atlas_index)
{
    if( !set )
        return NULL;

    struct TestSpriteAtlas const* atlas = NULL;
    if( scene_id == TEST_SCENE_SIDEICONS )
        atlas = &set->sideicons;
    else if( scene_id == TEST_SCENE_REDSTONE )
        atlas = &set->redstone;
    else
        return NULL;

    if( !atlas->sprites || atlas_index < 0 || atlas_index >= atlas->count )
        return NULL;
    return atlas->sprites[atlas_index];
}
