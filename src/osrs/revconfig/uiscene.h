#ifndef UISCENE_H
#define UISCENE_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

enum UISceneEventType
{
    UISCENE_EVENT_NONE = 0,
    UISCENE_EVENT_ELEMENT_ACQUIRED = 1,
    UISCENE_EVENT_ELEMENT_RELEASED = 2,
    UISCENE_EVENT_FONT_ADDED = 3,
    /** Begin batching sprite atlas uploads under `batch_id`. */
    UISCENE_EVENT_BATCH_SPRITE_BEGIN = 4,
    /** End sprite batch; consumer packs atlas and uploads GPU texture. */
    UISCENE_EVENT_BATCH_SPRITE_END = 5,
    /** Begin batching font atlas uploads under `batch_id`. */
    UISCENE_EVENT_BATCH_FONT_BEGIN = 6,
    /** End font batch; consumer packs atlas and uploads GPU texture. */
    UISCENE_EVENT_BATCH_FONT_END = 7,
    /** GPU should load `model` for `element_id` (still owned by the element until release). */
    UISCENE_EVENT_MODEL_LOAD = 8,
    /** GPU should unload `model`; UIScene no longer holds it — consumer frees after unload. */
    UISCENE_EVENT_MODEL_UNLOAD = 9,
};

struct UISceneEvent
{
    enum UISceneEventType type;
    int element_id;
    int parent_entity_id;
    int font_id;
    uint32_t batch_id; ///< For BATCH_SPRITE_BEGIN/END and BATCH_FONT_BEGIN/END
    /** ELEMENT_RELEASED only: snapshot of sprite pointers (malloc'd); consumer frees array +
     * DashSprites after unload. */
    struct DashSprite** released_sprites;
    int released_sprites_count;
    /** MODEL_LOAD / MODEL_UNLOAD: DashModel pointer. LOAD: still owned by the element. UNLOAD:
     * ownership passed to the consumer (free after RES_MODEL_UNLOAD or if the event is discarded).
     */
    struct DashModel* model;
};

struct UISceneElement
{
    int id;
    bool active;
    int parent_entity_id;
    struct UISceneElement* next;
    struct UISceneElement* prev;

    char name[64];
    struct DashSprite** dash_sprites;
    int dash_sprites_count;
    struct DashModel* dash_model;
};

#define UISCENE_FONT_MAX 16
#define UISCENE_FONT_NAME_MAX 64

struct UISceneFontSlot
{
    char name[UISCENE_FONT_NAME_MAX];
    struct DashPixFont* font;
};

struct UIScene
{
    // Intrusive list. Elements point to their next element.
    struct UISceneElement* elements;
    int elements_count;

    struct UISceneElement* active_list;
    int active_len;
    struct UISceneElement* free_list;
    int free_len;

    struct UISceneEvent* eventbuffer;
    int eventbuffer_capacity;
    int eventbuffer_head;
    int eventbuffer_tail;
    int eventbuffer_count;

    struct UISceneFontSlot fonts[UISCENE_FONT_MAX];
    int font_count;

    /** Active batch IDs (0 = none). */
    uint32_t active_sprite_batch_id;
    uint32_t active_font_batch_id;
};

/** High bit of the `visual_id` portion of `model_key` for UIScene-hosted models (disjoint from
 * Scene2 TRSPK visual ids). Requires `element_id` in [0, 0x7FFFFF]. */
#define UISCENE_MODEL_CACHE_VISUAL_TAG ((int)(1u << 23))

/** Static bind-pose key: anim_id and frame_index zero. See `torirs_model_cache_key_decode`. */
static inline uint64_t
uiscene_model_cache_key(int element_id)
{
    if( element_id < 0 )
        return 0;
    unsigned el = (unsigned)element_id;
    if( el > 0x7FFFFFu )
        el = 0x7FFFFFu;
    const int visual = UISCENE_MODEL_CACHE_VISUAL_TAG | (int)el;
    return ((uint64_t)(uint32_t)visual << 24);
}

struct UIScene*
uiscene_new(int size);

/** Frees scene elements and every non-NULL DashPixFont in font slots (UIScene owns fonts). Callers
 * replacing a scene must clear font pointers they keep (see lua_ui_reset_uiscene_and_refs). */
void
uiscene_free(struct UIScene* uiscene);

int
uiscene_element_acquire(
    struct UIScene* uiscene,
    int parent_entity_id);

/** Acquires an element AND assigns dash_sprites/name BEFORE pushing the ELEMENT_ACQUIRED event.
 *  This eliminates the window where a consumer can observe the element with dash_sprites==NULL.
 *  - sprites: caller transfers ownership of the pointer array and each non-NULL DashSprite
 *    (UIScene frees them on release / scene free).
 *  - name: optional, may be NULL; truncated to 63 chars. */
int
uiscene_element_acquire_with_sprites(
    struct UIScene* uiscene,
    int parent_entity_id,
    struct DashSprite** sprites,
    int sprites_count,
    const char* name);

/** Like `uiscene_element_acquire_with_sprites` but attaches a single DashModel before
 * ELEMENT_ACQUIRED. Caller transfers ownership of `model`. */
int
uiscene_element_acquire_with_model(
    struct UIScene* uiscene,
    int parent_entity_id,
    struct DashModel* model,
    const char* name);

void
uiscene_element_release(
    struct UIScene* uiscene,
    int element_id);

struct UISceneElement*
uiscene_element_at(
    struct UIScene* uiscene,
    int element_id);

/** Active element's model or NULL (inactive / out of range). */
struct DashModel*
uiscene_element_dash_model(
    struct UIScene* uiscene,
    int element_id);

/** First active element whose `name` matches; sprite at atlas_index or NULL. */
struct DashSprite*
uiscene_sprite_by_name(
    struct UIScene* uiscene,
    const char* name,
    int atlas_index);

/** First active element whose `name` matches; returns element id or -1. */
int
uiscene_element_id_by_name(
    struct UIScene* uiscene,
    const char* name);

bool
uiscene_eventbuffer_is_empty(struct UIScene* uiscene);

bool
uiscene_eventbuffer_pop(
    struct UIScene* uiscene,
    struct UISceneEvent* out_event);

void
uiscene_eventbuffer_clear(struct UIScene* uiscene);

/** Register a font under `name`. Returns dense id in [0, font_count), or -1 on error.
 * Duplicate name updates the pointer and returns the existing id (no second FONT_ADDED event). */
int
uiscene_font_add(
    struct UIScene* uiscene,
    const char* name,
    struct DashPixFont* font);

struct DashPixFont*
uiscene_font_get(
    struct UIScene* uiscene,
    int font_id);

/** Returns font id or -1 if not found. */
int
uiscene_font_find_id(
    struct UIScene* uiscene,
    const char* name);

/** Open a sprite atlas batch. All ELEMENT_ACQUIRED events inside until
 * uiscene_sprite_batch_end() are tagged with this batch_id. */
void
uiscene_sprite_batch_begin(
    struct UIScene* uiscene,
    uint32_t batch_id);

/** Close the active sprite batch. */
void
uiscene_sprite_batch_end(struct UIScene* uiscene);

/** Open a font batch. All FONT_ADDED events inside until
 * uiscene_font_batch_end() are tagged with this batch_id. */
void
uiscene_font_batch_begin(
    struct UIScene* uiscene,
    uint32_t batch_id);

/** Close the active font batch. */
void
uiscene_font_batch_end(struct UIScene* uiscene);

#endif
