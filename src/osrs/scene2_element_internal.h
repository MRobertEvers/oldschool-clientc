#ifndef SCENE2_ELEMENT_INTERNAL_H
#define SCENE2_ELEMENT_INTERNAL_H

#include "scene2.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define SCENE2_FLAG_VALID 0x80000000u
#define SCENE2_FLAG_TILE 0x40000000u
#define SCENE2_FLAG_ACTIVE 0x20000000u
#define SCENE2_PARENT_MASK 0x00FFFFFFu
#define SCENE2_PARENT_NONE 0x00FFFFFFu

struct Scene2ElementTile
{
    uint32_t flags_entity;
    struct Scene2Element* prev;
    struct Scene2Element* next;
    struct DashModel* dash_model;
    struct DashPosition* dash_position;
};

struct Scene2ElementFull
{
    uint32_t flags_entity;
    struct Scene2Element* prev;
    struct Scene2Element* next;
    struct DashModel* dash_model;
    struct DashPosition* dash_position;
    struct DashFramemap* dash_framemap;
    uint16_t active_anim_id;
    uint8_t active_frame;
    uint8_t _pad;
    struct Scene2Frames primary_frames;
    struct Scene2Frames secondary_frames;
};

static inline uint32_t
scene2__flags_raw(const struct Scene2Element* e)
{
    return *(const uint32_t*)(const void*)e;
}

static inline bool
scene2__is_tile(const struct Scene2Element* e)
{
    assert((scene2__flags_raw(e) & SCENE2_FLAG_VALID) && "Scene2Element: invalid or uninitialized");
    return (scene2__flags_raw(e) & SCENE2_FLAG_TILE) != 0;
}

static inline struct Scene2ElementTile*
scene2__as_tile(struct Scene2Element* e)
{
    assert(scene2__is_tile(e));
    return (struct Scene2ElementTile*)e;
}

static inline struct Scene2ElementFull*
scene2__as_full(struct Scene2Element* e)
{
    assert((scene2__flags_raw(e) & SCENE2_FLAG_VALID) && "Scene2Element: invalid or uninitialized");
    assert(!scene2__is_tile(e));
    return (struct Scene2ElementFull*)e;
}

static inline const struct Scene2ElementTile*
scene2__as_tile_const(const struct Scene2Element* e)
{
    assert(scene2__is_tile(e));
    return (const struct Scene2ElementTile*)e;
}

static inline const struct Scene2ElementFull*
scene2__as_full_const(const struct Scene2Element* e)
{
    assert((scene2__flags_raw(e) & SCENE2_FLAG_VALID) && "Scene2Element: invalid or uninitialized");
    assert(!scene2__is_tile(e));
    return (const struct Scene2ElementFull*)e;
}

#endif
