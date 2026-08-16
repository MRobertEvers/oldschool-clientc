#ifndef TEST_SPRITES_H
#define TEST_SPRITES_H

#include "toridraw/toridraw_sprite.h"

enum TestSpriteScene
{
    TEST_SCENE_SIDEICONS = 1,
    TEST_SCENE_REDSTONE = 2,
};

enum TestRedstoneAtlas
{
    TEST_REDSTONE_1 = 0,
    TEST_REDSTONE_2,
    TEST_REDSTONE_3,
    TEST_REDSTONE_2H,
    TEST_REDSTONE_1H,
    TEST_REDSTONE_2V,
    TEST_REDSTONE_3V,
    TEST_REDSTONE_2HV,
    TEST_REDSTONE_1HV,
    TEST_REDSTONE_ATLAS_COUNT
};

#define TEST_SIDEICON_COUNT 14
#define TEST_ICON_SIZE 32

struct TestSpriteAtlas
{
    struct ToriDraw_Sprite** sprites;
    int count;
};

struct TestSpriteSet
{
    struct TestSpriteAtlas sideicons;
    struct TestSpriteAtlas redstone;
};

bool
test_sprites_create(struct TestSpriteSet* out);

void
test_sprites_free(struct TestSpriteSet* set);

struct ToriDraw_Sprite*
test_sprites_get(
    struct TestSpriteSet const* set,
    int scene_id,
    int atlas_index);

#endif
