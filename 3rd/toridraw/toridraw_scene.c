#include "toridraw_scene.h"

#include "toridraw_animation.h"
#include "toridraw_font.h"
#include "toridraw_map.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"
#include "toridraw_shared_model.h"
#include "toridraw_sprite.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#define TD_SCENE_BATCH_ELEMENT_HANDLE_INVALID(HANDLE, SCENE) \
    do { \
        (HANDLE).scene = (SCENE); \
        (HANDLE).batch_id = TORIDRAW_SCENE_INVALID_BATCH_ID; \
        (HANDLE).id = TORIDRAW_SCENE_INVALID_ELEMENT_ID; \
    } while(0)
// clang-format on

struct MapEntry_ToriModel
{
    int id;
    struct ToriDraw_ModelHandle model;
};

struct MapEntry_Animation
{
    int id;
    struct ToriDraw_Animation* animation;
};

struct MapEntry_Sprite
{
    int id;
    struct ToriDraw_Sprite** sprites;
    int count;
};

struct MapEntry_Font
{
    int id;
    struct ToriDraw_Font* font;
};

static void
td_scene_ui_assets_changed(struct ToriDraw_Scene* scene)
{
    assert(scene);
    scene->ui_asset_revision++;
    if( scene->ui_asset_revision == 0 )
        scene->ui_asset_revision++;
}

uint64_t
ToriDraw_SceneUIAssetRevision(struct ToriDraw_Scene const* scene)
{
    assert(scene);
    return scene->ui_asset_revision;
}

struct MapEntry_Sound
{
    int id;
    struct ToriDraw_Sound* sound;
};


struct ToriDraw_ScenePendingPose
{
    struct ToriDraw_Model* model;
};

static struct ToriDraw_Map*
td_scene_map_new(
    int entry_size,
    int capacity)
{
    int buffer_size = ToriDraw_MapBufferSizeFor(entry_size, capacity);
    struct ToriDraw_MapConfig config = {
        .buffer = malloc(buffer_size),
        .buffer_size = buffer_size,
        .key_size = sizeof(int),
        .entry_size = entry_size,
    };
    return ToriDraw_MapNew(&config, 0);
}

static void
td_scene_map_free(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    free(ToriDraw_MapBufferPtr(map));
    ToriDraw_MapFree(map);
}

static void
td_scene_map_reset(
    struct ToriDraw_Map** map_out,
    int entry_size,
    int capacity)
{
    assert(map_out);
    if( !*map_out )
        return;

    td_scene_map_free(*map_out);
    *map_out = td_scene_map_new(entry_size, capacity);
}

static void
td_scene_maybe_grow_hmap(struct ToriDraw_Map* map)
{
    assert(map);

    uint32_t count = ToriDraw_MapCount(map);
    uint32_t capacity = ToriDraw_MapCapacity(map);
    if( count * 4 <= capacity * 3 )
        return;

    size_t new_capacity = (size_t)capacity * 2;
    size_t esize = ToriDraw_MapEntrySize(map);
    size_t new_buffer_size = ToriDraw_MapBufferSizeFor(esize, new_capacity);
    void* new_buffer = malloc(new_buffer_size);
    assert(new_buffer);

    void* old_buffer = NULL;
    int rc = ToriDraw_MapResize(map, new_buffer, new_buffer_size, new_capacity, &old_buffer);
    assert(rc == TORIDRAW_MAP_OK);
    (void)rc;
    free(old_buffer);
}

static void
td_scene_prepare_hmap_insert(struct ToriDraw_Map* map)
{
    assert(map);

    uint32_t count = ToriDraw_MapCount(map);
    uint32_t capacity = ToriDraw_MapCapacity(map);
    if( capacity > 0 && count * 4 >= capacity * 3 )
        td_scene_maybe_grow_hmap(map);
}

static bool
td_scene_element_valid(
    const struct ToriDraw_Scene* scene,
    int element_id);

static struct ToriDraw_SceneElement*
td_scene_element_ptr(
    struct ToriDraw_Scene* scene,
    int element_id);

/* Makes room for one more event. The caller has already refused the push at
 * TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE, so this only ever grows toward it. */
static void
td_event_queue_reserve(struct ToriDraw_EventQueue* queue)
{
    struct ToriDraw_Event* grown;
    int next;

    assert(queue);
    assert(queue->count < TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE);
    if( queue->count < queue->cap )
        return;

    /* 3/2, not 2x. Doubling carries up to 100% dead capacity at the moment it
     * grows, and this queue stops at its high-water for the rest of the
     * session -- the last doubling overshot to the 32768-entry cap and held
     * 2.75 MB against a count well under it. 3/2 bounds the slack at 50% and
     * is still amortised O(1). */
    next = queue->cap ? queue->cap + queue->cap / 2 : 256;
    if( next > TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE )
        next = TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE;
    grown = realloc(queue->events, (size_t)next * sizeof(*queue->events));
    assert(grown);
    queue->events = grown;
    queue->cap = next;
}

static void
td_scene_emit(
    struct ToriDraw_Scene* scene,
    enum ToriDraw_EventKind kind,
    int batch_id,
    int element_id,
    int pose_id,
    int texture_id,
    const struct ToriDraw_ModelHandle* model,
    struct ToriDraw_Animation* animation,
    struct ToriDraw_Texture* texture)
{
    struct ToriDraw_Event event;

    assert(scene);
    if( scene->event_queue.count >= TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.batch_id = batch_id;
    event.element_id = element_id;
    event.pose_id = pose_id;
    event.texture_id = texture_id;
    if( model )
        event.model = *model;
    event.animation = animation;
    event.texture = texture;
    if( td_scene_element_valid(scene, element_id) )
    {
        struct ToriDraw_SceneElement* element = td_scene_element_ptr(scene, element_id);
        if( element )
            event.world_position = element->world_position;
    }

    td_event_queue_reserve(&scene->event_queue);
    scene->event_queue.events[scene->event_queue.count++] = event;
}

static void
td_scene_emit_sprite(
    struct ToriDraw_Scene* scene,
    enum ToriDraw_EventKind kind,
    int element_id,
    struct ToriDraw_Sprite** sprites,
    int count)
{
    struct ToriDraw_Event event;

    assert(scene);
    if( scene->event_queue.count >= TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.element_id = element_id;
    event.sprites = sprites;
    event.sprite_count = count;
    td_event_queue_reserve(&scene->event_queue);
    scene->event_queue.events[scene->event_queue.count++] = event;
}

static void
td_scene_emit_font(
    struct ToriDraw_Scene* scene,
    enum ToriDraw_EventKind kind,
    int font_id,
    struct ToriDraw_Font* font)
{
    struct ToriDraw_Event event;

    assert(scene);
    if( scene->event_queue.count >= TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.texture_id = font_id;
    event.font = font;
    td_event_queue_reserve(&scene->event_queue);
    scene->event_queue.events[scene->event_queue.count++] = event;
}

static void
td_scene_emit_sound(
    struct ToriDraw_Scene* scene,
    enum ToriDraw_EventKind kind,
    int sound_id,
    struct ToriDraw_Sound* sound)
{
    struct ToriDraw_Event event;

    assert(scene);
    if( scene->event_queue.count >= TORIDRAW_SCENE_EVENT_QUEUE_MAX_SIZE )
        return;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    event.sound_id = sound_id;
    event.sound = sound;
    td_event_queue_reserve(&scene->event_queue);
    scene->event_queue.events[scene->event_queue.count++] = event;
}

static void
td_scene_retain_pending_pose(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Model* model)
{
    assert(scene);
    assert(model);

    if( scene->pending_pose_count >= scene->pending_pose_cap )
    {
        int new_cap = scene->pending_pose_cap ? scene->pending_pose_cap * 2 : 64;
        struct ToriDraw_ScenePendingPose* grown = realloc(
            scene->pending_poses, (size_t)new_cap * sizeof(struct ToriDraw_ScenePendingPose));
        if( !grown )
            return;
        scene->pending_poses = grown;
        scene->pending_pose_cap = new_cap;
    }

    scene->pending_poses[scene->pending_pose_count++].model = model;
}

static bool
td_scene_element_valid(
    const struct ToriDraw_Scene* scene,
    int element_id)
{
    assert(scene);
    /* The id may carry a kind in its top bits; the list is indexed by the
     * bottom ones. Untagged ids are kind NONE and mask to themselves. */
    element_id = ToriDraw_ElementIndexOfRaw(element_id);
    if( element_id < 0 || element_id >= TORIDRAW_SCENE_MAX_ELEMENTS )
        return false;
    return ToriDraw_IntrusiveListIsLive(&scene->elements, element_id);
}

static struct ToriDraw_SceneElement*
td_scene_element_ptr(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    if( !td_scene_element_valid(scene, element_id) )
        return NULL;
    return (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListGet(
        &scene->elements, ToriDraw_ElementIndexOfRaw(element_id));
}

static void
td_scene_reset_element(struct ToriDraw_SceneElement* element)
{
    assert(element);
    int scene_id = element->scene_id;
    memset(element, 0, sizeof(*element));
    element->scene_id = scene_id;
    element->anim_seq_id = -1;
}

static int
td_scene_allocate_element_id(
    struct ToriDraw_Scene* scene,
    int pool)
{
    struct ToriDraw_SceneElement* element;
    int id;

    assert(scene);
    /* The pool tag is one byte on the element; a view pool past the end would
     * wrap onto another view's elements and free them on that view's clear. */
    assert(pool >= 0);
    assert(pool < 256);

    if( scene->elements.free_head != TORIDRAW_INTRUSIVE_NIL )
    {
        element = (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListAt(
            &scene->elements, scene->elements.free_head);
        td_scene_reset_element(element);
    }
    else
    {
        if( scene->elements.count >= TORIDRAW_SCENE_MAX_ELEMENTS )
            return -1;
        /* calloc already hands back the zeroed state td_scene_reset_element
         * would write, and scene_id is assigned below on both paths, so only
         * the one non-zero default is left to set. */
        element = calloc(1, sizeof(struct ToriDraw_SceneElement));
        assert(element);
        element->anim_seq_id = -1;
    }

    id = ToriDraw_IntrusiveListAlloc(&scene->elements, element);
    if( id < 0 )
    {
        if( scene->elements.free_head == TORIDRAW_INTRUSIVE_NIL )
            free(element);
        return -1;
    }

    element->scene_id = id;
    element->pool = (uint8_t)pool;
    scene->anim_list_dirty = true;
    return id;
}

struct ToriDraw_SharedFacesStore*
ToriDraw_SceneSharedFaces(struct ToriDraw_Scene* scene)
{
    assert(scene);
    if( !scene->shared_faces )
        scene->shared_faces = ToriDraw_SharedFacesStoreNew();
    return scene->shared_faces;
}

struct ToriDraw_SharedModelStore*
ToriDraw_SceneSharedModels(struct ToriDraw_Scene* scene)
{
    assert(scene);
    if( !scene->shared_models )
        scene->shared_models = ToriDraw_SharedModelStoreNew();
    return scene->shared_models;
}

struct ToriDraw_Model*
ToriDraw_SceneElementModelForWrite(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* element;
    struct ToriDraw_Model* model;

    assert(scene);

    if( element_id < 0 || !ToriDraw_SceneElementIsLive(scene, element_id) )
        return NULL;
    element = ToriDraw_SceneElementGet(scene, element_id);
    if( !element || !ToriDraw_ModelKindIsFull(element->model.kind) )
        return NULL;
    /* Already ours: nothing to un-share. */
    if( element->model.kind == TORIDRAWMK_MODEL || element->model.kind == TORIDRAWMK_MODEL_HD )
        return element->model.u.model.model;

    /* Give this element geometry it can edit and hand the loan back. The copy
     * carries the bind pose, so an element about to be animated is no worse off
     * for being copied here than it would have been built unshared.
     *
     * The element changes TYPE here -- ToriDraw_ModelCopy reads the shared
     * geometry and returns one that owns itself -- which is the whole job of
     * this function, and is now a fact about its handle rather than about two
     * fields nobody had to look at. */
    model = ToriDraw_ModelCopy(ToriDraw_ModelRead(element->model));
    ToriDraw_ModelHandleFree(element->model);
    element->model = ToriDraw_ModelHandleOwned(model);
    return model;
}

static void
td_scene_dispose_element_model(struct ToriDraw_SceneElement* element)
{
    assert(element);
    /* Whatever the element holds, by its kind: an owned model outright, a
     * shared one by dropping a holder, a lent-faces one by dropping its own
     * arrays and its share of the loan. */
    ToriDraw_ModelHandleFree(element->model);
    element->model.kind = TORIDRAWMK_NONE;
    element->model.u.model.model = NULL;
}

static void
td_scene_free_element_id(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* element;

    if( !td_scene_element_valid(scene, element_id) )
        return;

    element = td_scene_element_ptr(scene, element_id);
    if( element )
    {
        if( element->anim_seq_id != -1 )
        {
            td_scene_emit(
                scene,
                TORIDRAW_EVENT_ANIM_UNLOAD,
                0,
                element_id,
                0,
                0,
                ToriDraw_ModelKindIsFull(element->model.kind) ? &element->model : NULL,
                NULL,
                NULL);
        }
        td_scene_dispose_element_model(element);
        td_scene_reset_element(element);
    }

    ToriDraw_IntrusiveListRelease(&scene->elements, element_id);
    scene->anim_list_dirty = true;
}

static void
td_scene_free_models_map(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_ToriModel* entry = NULL;
    while( (entry = (struct MapEntry_ToriModel*)ToriDraw_MapIterNext(iter)) )
    {
        ToriDraw_ModelHandleFree(entry->model);
    }
    ToriDraw_MapIterFree(iter);
}

static void
td_scene_free_animations_map(struct ToriDraw_Map* map)
{
    if( !map )
        return;

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(map);
    struct MapEntry_Animation* entry = NULL;
    while( (entry = (struct MapEntry_Animation*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->animation )
            ToriDraw_AnimationFree(entry->animation);
    }
    ToriDraw_MapIterFree(iter);
}

static void
td_scene_free_textures(struct ToriDraw_TextureMap* map)
{
    if( !map )
        return;

    for( int i = 0; i < TORIDRAW_TEXTURE_ID_CAPACITY; i++ )
    {
        if( map->textures[i] )
            ToriDraw_TextureFree(map->textures[i]);
        map->textures[i] = NULL;
    }
    map->count = 0;
}

static void
td_scene_free_all_elements(struct ToriDraw_Scene* scene)
{
    int i;

    if( !scene )
        return;

    for( i = 0; i < scene->elements.count; i++ )
    {
        struct ToriDraw_SceneElement* element =
            (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListGet(&scene->elements, i);
        if( !element )
            continue;
        if( ToriDraw_IntrusiveListIsLive(&scene->elements, i) )
            td_scene_dispose_element_model(element);
        free(element);
    }
    ToriDraw_IntrusiveListFree(&scene->elements);
}

bool
ToriDraw_SceneGraphInit(struct ToriDraw_Scene* scene)
{
    assert(scene);

    scene->ui_asset_revision = 0;

    scene->models_hmap = td_scene_map_new(sizeof(struct MapEntry_ToriModel), 1024);
    scene->animation_hmap = td_scene_map_new(sizeof(struct MapEntry_Animation), 512);
    scene->sprites_hmap = td_scene_map_new(sizeof(struct MapEntry_Sprite), 1024);
    scene->fonts_hmap = td_scene_map_new(sizeof(struct MapEntry_Font), 16);
    scene->sounds_hmap = td_scene_map_new(sizeof(struct MapEntry_Sound), 64);
    ToriDraw_IntrusiveListInit(&scene->elements);

    if( !scene->models_hmap || !scene->animation_hmap || !scene->sprites_hmap ||
        !scene->fonts_hmap || !scene->sounds_hmap )
    {
        ToriDraw_SceneGraphShutdown(scene);
        return false;
    }

    return true;
}

void
ToriDraw_SceneGraphShutdown(struct ToriDraw_Scene* scene)
{
    assert(scene);

    td_scene_free_all_elements(scene);

    for( int i = 0; i < scene->pending_pose_count; i++ )
    {
        if( scene->pending_poses[i].model )
            ToriDraw_ModelFree(scene->pending_poses[i].model);
    }
    free(scene->pending_poses);

    td_scene_free_models_map(scene->models_hmap);
    td_scene_map_free(scene->models_hmap);

    td_scene_free_animations_map(scene->animation_hmap);
    td_scene_map_free(scene->animation_hmap);

    if( scene->sprites_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(scene->sprites_hmap);
        struct MapEntry_Sprite* entry = NULL;
        while( (entry = (struct MapEntry_Sprite*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->sprites )
            {
                for( int i = 0; i < entry->count; i++ )
                    ToriDraw_SpriteFree(entry->sprites[i]);
                free(entry->sprites);
            }
        }
        ToriDraw_MapIterFree(iter);
        td_scene_map_free(scene->sprites_hmap);
    }

    if( scene->fonts_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(scene->fonts_hmap);
        struct MapEntry_Font* entry = NULL;
        while( (entry = (struct MapEntry_Font*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->font )
                ToriDraw_FontFree(entry->font);
        }
        ToriDraw_MapIterFree(iter);
        td_scene_map_free(scene->fonts_hmap);
    }

    if( scene->sounds_hmap )
    {
        struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(scene->sounds_hmap);
        struct MapEntry_Sound* entry = NULL;
        while( (entry = (struct MapEntry_Sound*)ToriDraw_MapIterNext(iter)) )
        {
            if( entry->sound )
                ToriDraw_SoundFree(entry->sound);
        }
        ToriDraw_MapIterFree(iter);
        td_scene_map_free(scene->sounds_hmap);
    }

    if( scene->tex_state )
        td_scene_free_textures(&scene->tex_state->texture_map);

    scene->models_hmap = NULL;
    scene->animation_hmap = NULL;
    scene->sprites_hmap = NULL;
    scene->fonts_hmap = NULL;
    scene->sounds_hmap = NULL;
    memset(scene->cache_fonts, 0, sizeof(scene->cache_fonts));
    scene->pending_poses = NULL;
}

void
ToriDraw_SceneSpriteAdd(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_Sprite** sprites,
    int count)
{
    assert(scene);
    assert(sprites);
    if( element_id < 0 || count <= 0 )
        return;

    td_scene_prepare_hmap_insert(scene->sprites_hmap);
    struct MapEntry_Sprite* entry = (struct MapEntry_Sprite*)ToriDraw_MapSearch(
        scene->sprites_hmap, &element_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;
    /* No post-search grow: it reallocates the slot buffer and would dangle the
     * entry pointer the writes below use. `td_scene_prepare_hmap_insert` above
     * already guarantees room. See ToriDraw_SceneSoundAdd for what the dangling
     * write actually looks like when it happens. */

    if( entry->sprites )
    {
        td_scene_emit_sprite(
            scene, TORIDRAW_EVENT_SPRITE_UNLOAD, element_id, entry->sprites, entry->count);
        for( int i = 0; i < entry->count; i++ )
            ToriDraw_SpriteFree(entry->sprites[i]);
        free(entry->sprites);
    }

    entry->id = element_id;
    entry->sprites = sprites;
    entry->count = count;
    td_scene_emit_sprite(scene, TORIDRAW_EVENT_SPRITE_LOAD, element_id, sprites, count);
    td_scene_ui_assets_changed(scene);
}

void
ToriDraw_SceneSpriteRemove(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    assert(scene);

    struct MapEntry_Sprite* entry = (struct MapEntry_Sprite*)ToriDraw_MapSearch(
        scene->sprites_hmap, &element_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return;

    /* Same teardown SceneSpriteAdd does when it replaces an entry — the unload
     * event is what lets a renderer drop the GPU copy, so a caller that evicts
     * without replacing (the world map's region pool) must go through here
     * rather than leaving the entry behind. */
    if( entry->sprites )
    {
        td_scene_emit_sprite(
            scene, TORIDRAW_EVENT_SPRITE_UNLOAD, element_id, entry->sprites, entry->count);
        for( int i = 0; i < entry->count; i++ )
            ToriDraw_SpriteFree(entry->sprites[i]);
        free(entry->sprites);
    }
    entry->sprites = NULL;
    entry->count = 0;
    ToriDraw_MapSearch(scene->sprites_hmap, &element_id, TORIDRAW_MAP_REMOVE);
    td_scene_ui_assets_changed(scene);
}

struct ToriDraw_Sprite**
ToriDraw_SceneSpriteGet(
    struct ToriDraw_Scene* scene,
    int element_id,
    int* out_count)
{
    if( out_count )
        *out_count = 0;
    assert(scene);

    struct MapEntry_Sprite* entry = (struct MapEntry_Sprite*)ToriDraw_MapSearch(
        scene->sprites_hmap, &element_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    if( out_count )
        *out_count = entry->count;
    return entry->sprites;
}

bool
ToriDraw_SceneSpriteHas(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    return ToriDraw_SceneSpriteGet(scene, element_id, NULL) != NULL;
}

void
ToriDraw_SceneFontAdd(
    struct ToriDraw_Scene* scene,
    int font_id,
    struct ToriDraw_Font* font)
{
    assert(scene);
    assert(font);
    if( font_id < 0 )
        return;

    td_scene_prepare_hmap_insert(scene->fonts_hmap);
    struct MapEntry_Font* entry =
        (struct MapEntry_Font*)ToriDraw_MapSearch(scene->fonts_hmap, &font_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;
    /* No post-search grow: it reallocates the slot buffer and would dangle the
     * entry pointer the writes below use. `td_scene_prepare_hmap_insert` above
     * already guarantees room. See ToriDraw_SceneSoundAdd for what the dangling
     * write actually looks like when it happens. */

    if( entry->font )
    {
        td_scene_emit_font(scene, TORIDRAW_EVENT_FONT_UNLOAD, font_id, entry->font);
        ToriDraw_FontFree(entry->font);
    }

    entry->id = font_id;
    entry->font = font;
    td_scene_emit_font(scene, TORIDRAW_EVENT_FONT_LOAD, font_id, font);
    td_scene_ui_assets_changed(scene);
}

/* --- sound assets --------------------------------------------------------- */

struct ToriDraw_Sound*
ToriDraw_SoundNew(
    int16_t* samples,
    int sample_count,
    int sample_rate,
    int loop_start,
    int loop_end,
    bool ping_pong,
    int queue_delay)
{
    struct ToriDraw_Sound* sound;

    if( !samples || sample_count <= 0 )
    {
        free(samples);
        return NULL;
    }
    sound = calloc(1, sizeof(*sound));
    assert(sound);
    sound->samples = samples;
    sound->sample_count = sample_count;
    sound->sample_rate = sample_rate;
    sound->loop_start = loop_start;
    sound->loop_end = loop_end;
    sound->ping_pong = ping_pong;
    sound->queue_delay = queue_delay;
    return sound;
}

void
ToriDraw_SoundFree(struct ToriDraw_Sound* sound)
{
    if( !sound )
        return;
    free(sound->samples);
    free(sound);
}

void
ToriDraw_SceneSoundAdd(
    struct ToriDraw_Scene* scene,
    int sound_id,
    struct ToriDraw_Sound* sound)
{
    assert(scene);
    if( sound_id < 0 || !sound )
    {
        ToriDraw_SoundFree(sound);
        return;
    }

    /*
     * Grow *before* the search and never after it.
     *
     * `td_scene_maybe_grow_hmap` reallocates the map's slot buffer, so an entry
     * pointer taken before it dangles afterwards -- and the writes below would
     * land in freed memory while the copy that survived the rehash kept an
     * uninitialised `sound`. The symptom is not a crash: the clip is simply not
     * in the map, the next lookup misses, and the previous one leaks. It only
     * bites on the two adds that straddle a growth threshold, which is why it
     * survives casual testing.
     *
     * `td_scene_prepare_hmap_insert` already grows whenever the map is at 3/4,
     * so there is always room for the insert that follows.
     */
    td_scene_prepare_hmap_insert(scene->sounds_hmap);
    struct MapEntry_Sound* existing = (struct MapEntry_Sound*)ToriDraw_MapSearch(
        scene->sounds_hmap, &sound_id, TORIDRAW_MAP_FIND);
    struct MapEntry_Sound* entry;

    /*
     * A fresh slot's payload is uninitialised (the map's buffer is malloc'd and
     * INSERT only writes the key), so `entry->sound` is only meaningful when the
     * key was already present. FIND first, then INSERT -- they return the same
     * slot for an existing key.
     */
    if( existing && existing->sound )
    {
        /* Unload before free: the event carries the id, and a backend that
         * copied the samples needs to be told to drop its copy before this one
         * goes away. */
        td_scene_emit_sound(scene, TORIDRAW_EVENT_SOUND_UNLOAD, sound_id, existing->sound);
        ToriDraw_SoundFree(existing->sound);
        existing->sound = NULL;
    }

    entry = (struct MapEntry_Sound*)ToriDraw_MapSearch(
        scene->sounds_hmap, &sound_id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        ToriDraw_SoundFree(sound);
        return;
    }

    entry->id = sound_id;
    entry->sound = sound;
    td_scene_emit_sound(scene, TORIDRAW_EVENT_SOUND_LOAD, sound_id, sound);
}

struct ToriDraw_Sound*
ToriDraw_SceneSoundGet(
    struct ToriDraw_Scene* scene,
    int sound_id)
{
    struct MapEntry_Sound* entry;

    assert(scene);
    if( sound_id < 0 || !scene->sounds_hmap )
        return NULL;
    entry = (struct MapEntry_Sound*)ToriDraw_MapSearch(
        scene->sounds_hmap, &sound_id, TORIDRAW_MAP_FIND);
    return entry ? entry->sound : NULL;
}

bool
ToriDraw_SceneSoundHas(
    struct ToriDraw_Scene* scene,
    int sound_id)
{
    return ToriDraw_SceneSoundGet(scene, sound_id) != NULL;
}

void
ToriDraw_SceneSoundRemove(
    struct ToriDraw_Scene* scene,
    int sound_id)
{
    struct MapEntry_Sound* entry;

    assert(scene);
    if( sound_id < 0 || !scene->sounds_hmap )
        return;
    entry = (struct MapEntry_Sound*)ToriDraw_MapSearch(
        scene->sounds_hmap, &sound_id, TORIDRAW_MAP_FIND);
    if( !entry || !entry->sound )
        return;
    td_scene_emit_sound(scene, TORIDRAW_EVENT_SOUND_UNLOAD, sound_id, entry->sound);
    ToriDraw_SoundFree(entry->sound);
    entry->sound = NULL;
    entry->id = -1;
}

void
ToriDraw_SceneSoundsReemitLoads(struct ToriDraw_Scene* scene)
{
    struct ToriDraw_MapIter* iter;
    struct MapEntry_Sound* entry = NULL;

    assert(scene);
    if( !scene->sounds_hmap )
        return;
    iter = ToriDraw_MapIterNew(scene->sounds_hmap);
    while( (entry = (struct MapEntry_Sound*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sound )
            td_scene_emit_sound(scene, TORIDRAW_EVENT_SOUND_LOAD, entry->id, entry->sound);
    }
    ToriDraw_MapIterFree(iter);
}

struct ToriDraw_Font*
ToriDraw_SceneFontGet(
    struct ToriDraw_Scene* scene,
    int font_id)
{
    assert(scene);

    struct MapEntry_Font* entry =
        (struct MapEntry_Font*)ToriDraw_MapSearch(scene->fonts_hmap, &font_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->font;
}

bool
ToriDraw_SceneFontHas(
    struct ToriDraw_Scene* scene,
    int font_id)
{
    return ToriDraw_SceneFontGet(scene, font_id) != NULL;
}

void
ToriDraw_SceneCacheFontSet(
    struct ToriDraw_Scene* scene,
    int cache_font_id,
    struct ToriDraw_Font* font)
{
    assert(scene);
    if( cache_font_id < 0 || cache_font_id >= TORIDRAW_CACHE_FONT_SLOT_COUNT )
        return;
    scene->cache_fonts[cache_font_id] = font;
}

struct ToriDraw_Font*
ToriDraw_SceneCacheFontGet(
    struct ToriDraw_Scene* scene,
    int cache_font_id)
{
    assert(scene);
    if( cache_font_id < 0 || cache_font_id >= TORIDRAW_CACHE_FONT_SLOT_COUNT )
        return NULL;
    return scene->cache_fonts[cache_font_id];
}

void
ToriDraw_SceneModelAdd(
    struct ToriDraw_Scene* scene,
    int model_id,
    struct ToriDraw_ModelHandle model)
{
    td_scene_prepare_hmap_insert(scene->models_hmap);
    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)ToriDraw_MapSearch(
        scene->models_hmap, &model_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;
    /* No post-search grow: it reallocates the slot buffer and would dangle the
     * entry pointer the writes below use. `td_scene_prepare_hmap_insert` above
     * already guarantees room. See ToriDraw_SceneSoundAdd for what the dangling
     * write actually looks like when it happens. */

    entry->id = model_id;
    entry->model = model;
    td_scene_ui_assets_changed(scene);
}

struct ToriDraw_ModelHandle
ToriDraw_SceneModelGet(
    struct ToriDraw_Scene* scene,
    int model_id)
{
    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)ToriDraw_MapSearch(
        scene->models_hmap, &model_id, TORIDRAW_MAP_FIND);

    struct ToriDraw_ModelHandle model = { .kind = TORIDRAWMK_NONE };
    if( !entry )
        return model;

    return entry->model;
}

bool
ToriDraw_SceneModelHas(
    struct ToriDraw_Scene* scene,
    int model_id)
{
    return ToriDraw_ModelKindIsFull(ToriDraw_SceneModelGet(scene, model_id).kind);
}

struct ToriDraw_ModelHandle
ToriDraw_SceneModelRemove(
    struct ToriDraw_Scene* scene,
    int model_id)
{
    struct ToriDraw_ModelHandle none = { .kind = TORIDRAWMK_NONE };
    assert(scene);

    struct MapEntry_ToriModel* entry = (struct MapEntry_ToriModel*)ToriDraw_MapSearch(
        scene->models_hmap, &model_id, TORIDRAW_MAP_REMOVE);
    if( !entry )
        return none;

    td_scene_ui_assets_changed(scene);
    return entry->model;
}

void
ToriDraw_SceneModelsClearAll(struct ToriDraw_Scene* scene)
{
    bool changed;

    assert(scene);
    assert(scene->models_hmap);

    changed = ToriDraw_MapCount(scene->models_hmap) > 0;

    td_scene_free_models_map(scene->models_hmap);
    td_scene_map_reset(&scene->models_hmap, sizeof(struct MapEntry_ToriModel), 1024);
    if( changed )
        td_scene_ui_assets_changed(scene);
}

void
ToriDraw_SceneAnimationAdd(
    struct ToriDraw_Scene* scene,
    int anim_id,
    struct ToriDraw_Animation* animation)
{
    td_scene_prepare_hmap_insert(scene->animation_hmap);
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)ToriDraw_MapSearch(
        scene->animation_hmap, &anim_id, TORIDRAW_MAP_INSERT);
    if( !entry )
        return;
    /* No post-search grow: it reallocates the slot buffer and would dangle the
     * entry pointer the writes below use. `td_scene_prepare_hmap_insert` above
     * already guarantees room. See ToriDraw_SceneSoundAdd for what the dangling
     * write actually looks like when it happens. */

    /* First registration wins; a duplicate is freed instead of replacing it.
     *
     * The registry is not the only owner of a `ToriDraw_Animation*`. Scene
     * elements cache the resolved pointer directly (`element->animation`,
     * `element->secondary_animation`), and so does every render command already
     * built for this frame -- none of them are notified. Freeing the registered
     * animation to install a second copy of the SAME sequence therefore dangles
     * every element currently playing it, and the read that follows is
     * `animation->frames[frame].length` in ToriDraw_SceneElementApplyAnimation:
     * freed memory that reads <= 0 takes the hole-frame branch and resets the
     * model to its bind pose, and freed memory that reads > 0 poses it from a
     * garbage keyframe. "The model snaps back to bind pose now and then" is
     * exactly what that looks like.
     *
     * Duplicates are reachable because the load tasks dedupe against what is
     * *registered*, not against what is *in flight*: two SequenceLoad tasks for
     * one seq id can both be queued during the load window and both land here.
     * Nothing wants the second one -- the two decode the same archive to equal
     * data -- so keeping the pointer the elements already hold is both the safe
     * answer and the correct one. */
    if( entry->animation )
    {
        if( entry->animation != animation )
            ToriDraw_AnimationFree(animation);
        return;
    }

    entry->id = anim_id;
    entry->animation = animation;
}

struct ToriDraw_Animation*
ToriDraw_SceneAnimationGet(
    struct ToriDraw_Scene* scene,
    int anim_id)
{
    struct MapEntry_Animation* entry = (struct MapEntry_Animation*)ToriDraw_MapSearch(
        scene->animation_hmap, &anim_id, TORIDRAW_MAP_FIND);
    if( !entry )
        return NULL;
    return entry->animation;
}

bool
ToriDraw_SceneAnimationHas(
    struct ToriDraw_Scene* scene,
    int anim_id)
{
    return ToriDraw_SceneAnimationGet(scene, anim_id) != NULL;
}

void
ToriDraw_SceneSetTexture(
    struct ToriDraw_Scene* scene,
    int id,
    struct ToriDraw_Texture* texture)
{
    assert(scene);
    /* Cache data can name any id; out of range is a runtime reject, not an
     * assert. PublishTextures hands ownership of `texture`, so free it here. */
    if( id < 0 || id >= TORIDRAW_TEXTURE_ID_CAPACITY )
    {
        if( texture )
            ToriDraw_TextureFree(texture);
        return;
    }

    struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(scene);
    if( !tex_state )
    {
        if( texture )
            ToriDraw_TextureFree(texture);
        return;
    }

    struct ToriDraw_TextureMap* map = &tex_state->texture_map;
    struct ToriDraw_Texture* const old = map->textures[id];

    if( old == texture )
        return;

    if( old )
    {
        td_scene_emit(scene, TORIDRAW_EVENT_TEX_UNLOAD, 0, 0, 0, id, NULL, NULL, NULL);
        ToriDraw_TextureFree(old);
        map->textures[id] = NULL;
    }

    if( texture )
    {
        map->textures[id] = texture;
        td_scene_emit(scene, TORIDRAW_EVENT_TEX_LOAD, 0, 0, 0, id, NULL, NULL, texture);
        if( id >= map->count )
            map->count = id + 1;
    }
    else if( id == map->count - 1 )
    {
        while( map->count > 0 && !map->textures[map->count - 1] )
            map->count--;
    }
}

void
ToriDraw_SceneClear(struct ToriDraw_Scene* scene)
{
    int i;
    int next;

    assert(scene);

    for( i = scene->elements.head; i != TORIDRAW_INTRUSIVE_NIL; i = next )
    {
        struct ToriDraw_SceneElement* element;

        next = scene->elements.nodes[i].next;
        element = td_scene_element_ptr(scene, i);
        if( !element )
            continue;

        if( element->anim_seq_id != -1 )
        {
            td_scene_emit(
                scene,
                TORIDRAW_EVENT_ANIM_UNLOAD,
                0,
                i,
                0,
                0,
                ToriDraw_ModelKindIsFull(element->model.kind) ? &element->model : NULL,
                NULL,
                NULL);
        }
        td_scene_emit(
            scene,
            TORIDRAW_EVENT_MODEL_UNLOAD,
            0,
            i,
            0,
            0,
            ToriDraw_ModelKindIsFull(element->model.kind) ? &element->model : NULL,
            NULL,
            NULL);
        td_scene_dispose_element_model(element);
        td_scene_reset_element(element);
        ToriDraw_IntrusiveListRelease(&scene->elements, i);
    }

    scene->batch_building = false;
    scene->current_batch_id = 0;
    scene->current_batch_element_count = 0;
    scene->next_batch_id = 0;
    scene->anim_list_dirty = true;

    td_scene_emit(scene, TORIDRAW_EVENT_SCENE_RESET, 0, 0, 0, 0, NULL, NULL, NULL);
}

void
ToriDraw_SceneClearPool(
    struct ToriDraw_Scene* scene,
    int pool)
{
    int i;
    int next;
    bool clear_retained_batch;

    assert(scene);
    assert(pool >= 0);
    assert(pool < 256);
    /* Only view 0's static geometry goes into the retained batch arena, so
     * only its clear may drop the arena wholesale; every other pool (a boat
     * deck's static half, any view's entities) is unloaded element by element
     * so the arena — and with it the mainland — is left alone. */
    clear_retained_batch = pool == TORIDRAW_SCENE_POOL_STATIC;

    for( i = scene->elements.head; i != TORIDRAW_INTRUSIVE_NIL; i = next )
    {
        struct ToriDraw_SceneElement* element;

        next = scene->elements.nodes[i].next;
        element = td_scene_element_ptr(scene, i);
        if( !element || element->pool != (uint8_t)pool )
            continue;

        if( !clear_retained_batch && element->anim_seq_id != -1 )
        {
            td_scene_emit(
                scene,
                TORIDRAW_EVENT_ANIM_UNLOAD,
                0,
                i,
                0,
                0,
                ToriDraw_ModelKindIsFull(element->model.kind) ? &element->model : NULL,
                NULL,
                NULL);
        }
        if( !clear_retained_batch )
            td_scene_emit(
                scene,
                TORIDRAW_EVENT_MODEL_UNLOAD,
                0,
                i,
                0,
                0,
                ToriDraw_ModelKindIsFull(element->model.kind) ? &element->model : NULL,
                NULL,
                NULL);
        td_scene_dispose_element_model(element);
        td_scene_reset_element(element);
        ToriDraw_IntrusiveListRelease(&scene->elements, i);
    }

    scene->anim_list_dirty = true;

    if( pool == TORIDRAW_SCENE_POOL_STATIC )
    {
        scene->batch_building = false;
        scene->current_batch_id = 0;
        scene->current_batch_element_count = 0;
        scene->next_batch_id = 0;
        /* Static geometry and all prebaked poses share one retained arena.
         * Clear it wholesale on a map rebuild instead of issuing thousands of
         * append-only per-element unloads. */
        td_scene_emit(
            scene,
            TORIDRAW_EVENT_BATCH_CLEAR,
            TORIDRAW_SCENE_INVALID_BATCH_ID,
            0,
            0,
            0,
            NULL,
            NULL,
            NULL);
    }
}

int
ToriDraw_SceneElementAdd(struct ToriDraw_Scene* scene)
{
    assert(scene);
    return td_scene_allocate_element_id(scene, TORIDRAW_SCENE_POOL_STATIC);
}

int
ToriDraw_SceneElementAddPool(
    struct ToriDraw_Scene* scene,
    int pool)
{
    assert(scene);
    return td_scene_allocate_element_id(scene, pool);
}

void
ToriDraw_SceneElementSetPool(
    struct ToriDraw_Scene* scene,
    int element_id,
    int pool)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    /* Same bound as the allocator: the tag is one byte, and a pool past the end
     * would wrap onto another view's elements and be freed on that view's
     * clear. */
    assert(pool >= 0);
    assert(pool < 256);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    element->pool = (uint8_t)pool;
}

int
ToriDraw_SceneElementPool(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    assert(scene);
    if( !td_scene_element_valid(scene, element_id) )
        return -1;
    return (int)td_scene_element_ptr(scene, element_id)->pool;
}

int
ToriDraw_SceneElementRemove(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);

    if( !td_scene_element_valid(scene, element_id) )
        return -1;

    element = td_scene_element_ptr(scene, element_id);
    td_scene_emit(
        scene,
        TORIDRAW_EVENT_MODEL_UNLOAD,
        0,
        element_id,
        0,
        0,
        element && ToriDraw_ModelKindIsFull(element->model.kind) ? &element->model : NULL,
        NULL,
        NULL);

    td_scene_free_element_id(scene, element_id);
    return 0;
}

struct ToriDraw_SceneElement*
ToriDraw_SceneElementGet(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    return td_scene_element_ptr(scene, element_id);
}

void
ToriDraw_SceneElementPrefetchNode(
    const struct ToriDraw_Scene* scene,
    int element_id)
{
    int index;
    assert(scene);
    index = ToriDraw_ElementIndexOfRaw(element_id);
    if( index < 0 || index >= scene->elements.count )
        return;
    __builtin_prefetch(&scene->elements.nodes[index], 0, 1);
}

void
ToriDraw_SceneElementPrefetchData(
    const struct ToriDraw_Scene* scene,
    int element_id)
{
    int index;
    const void* data;
    assert(scene);
    index = ToriDraw_ElementIndexOfRaw(element_id);
    if( index < 0 || index >= scene->elements.count )
        return;
    data = scene->elements.nodes[index].data;
    if( data )
        __builtin_prefetch(data, 0, 1);
}

static const struct ToriDraw_Model*
td_scene_element_prefetch_model_of(
    const struct ToriDraw_Scene* scene,
    int element_id)
{
    int index;
    const struct ToriDraw_SceneElement* element;
    index = ToriDraw_ElementIndexOfRaw(element_id);
    if( index < 0 || index >= scene->elements.count )
        return NULL;
    element = scene->elements.nodes[index].data;
    if( !element || !ToriDraw_ModelKindIsFull(element->model.kind) )
        return NULL;
    return element->model.u.model.model;
}

void
ToriDraw_SceneElementPrefetchModel(
    const struct ToriDraw_Scene* scene,
    int element_id)
{
    const struct ToriDraw_Model* model;
    assert(scene);
    model = td_scene_element_prefetch_model_of(scene, element_id);
    if( !model )
        return;
    /* flags, counts and the array pointers sit in the first two lines; the
     * bounds cylinder some 270 bytes in. */
    __builtin_prefetch(model, 0, 1);
    __builtin_prefetch((const char*)model + 64, 0, 1);
    __builtin_prefetch(&model->bounds_cylinder, 0, 1);
}

void
ToriDraw_SceneElementPrefetchArrays(
    const struct ToriDraw_Scene* scene,
    int element_id)
{
    const struct ToriDraw_Model* model;
    assert(scene);
    model = td_scene_element_prefetch_model_of(scene, element_id);
    if( !model )
        return;
    if( model->vertices_x )
        __builtin_prefetch(model->vertices_x, 0, 1);
    if( model->vertices_y )
        __builtin_prefetch(model->vertices_y, 0, 1);
    if( model->vertices_z )
        __builtin_prefetch(model->vertices_z, 0, 1);
    if( model->face_indices_a )
        __builtin_prefetch(model->face_indices_a, 0, 1);
    if( model->face_indices_b )
        __builtin_prefetch(model->face_indices_b, 0, 1);
    if( model->face_indices_c )
        __builtin_prefetch(model->face_indices_c, 0, 1);
}

bool
ToriDraw_SceneElementIsLive(
    struct ToriDraw_Scene* scene,
    int element_id)
{
    return td_scene_element_valid(scene, element_id);
}

int
ToriDraw_SceneElementSlotCount(struct ToriDraw_Scene* scene)
{
    assert(scene);
    return scene->elements.count;
}

void
ToriDraw_SceneAnimListInvalidate(struct ToriDraw_Scene* scene)
{
    assert(scene);
    scene->anim_list_dirty = true;
}

int const*
ToriDraw_SceneAnimatedElements(
    struct ToriDraw_Scene* scene,
    int* out_count)
{
    assert(scene);
    assert(out_count);

    if( scene->anim_list_dirty )
    {
        /* Walk the live intrusive chain, not 0..elements.count. `count` is a
         * high-water slot index that never shrinks on free — scanning it made
         * every rebuild O(peak elements ever allocated), which climbed for the
         * life of a session whenever NPCs/spotanims churned through the pool. */
        int live = 0;
        for( int id = scene->elements.head; id != TORIDRAW_INTRUSIVE_NIL;
             id = scene->elements.nodes[id].next )
            live++;

        if( scene->anim_list_cap < live )
        {
            int cap = scene->anim_list_cap ? scene->anim_list_cap : 32;
            while( cap < live )
                cap <<= 1;
            {
                int* grown = (int*)realloc(scene->anim_list, (size_t)cap * sizeof(int));
                assert(grown);
                scene->anim_list = grown;
                scene->anim_list_cap = cap;
            }
        }

        scene->anim_list_count = 0;
        for( int id = scene->elements.head; id != TORIDRAW_INTRUSIVE_NIL;
             id = scene->elements.nodes[id].next )
        {
            struct ToriDraw_SceneElement const* element =
                (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListGet(&scene->elements, id);
            if( !element || element->anim_seq_id == -1 || element->anim_external )
                continue;
            scene->anim_list[scene->anim_list_count++] = id;
        }
        scene->anim_list_dirty = false;
    }

    *out_count = scene->anim_list_count;
    return scene->anim_list;
}

static void
td_scene_element_assign_model(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(ToriDraw_ModelKindIsFull(model.kind));
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    td_scene_dispose_element_model(element);
    element->model = model;

    if( scene->batch_building )
        element->pending_batch_add = true;
    else
        td_scene_emit(
            scene, TORIDRAW_EVENT_MODEL_LOAD, 0, element_id, 0, 0, &element->model, NULL, NULL);
}

/*
 * Whether this element can pose the model it is holding. Only an element that
 * can be posed needs a bind pose, and in a loaded region almost none can: the
 * ~19k elements are overwhelmingly static scenery.
 */
static bool
td_scene_element_is_animated(const struct ToriDraw_SceneElement* element)
{
    assert(element);
    return element->animation != NULL || element->secondary_animation != NULL ||
           element->skeletal_animation != NULL || element->anim_seq_id > 0 ||
           element->anim2_seq_id > 0;
}

/*
 * The pose every keyframe composes against. Idempotent on purpose: a model that
 * already carries a bind pose keeps it, or whatever pose it is currently in
 * would become the new bind -- the same corruption from the other direction.
 */
static void
td_ensure_bind_pose(struct ToriDraw_ModelHandle* handle)
{
    struct ToriDraw_Model* model;

    assert(handle);
    if( handle->kind != TORIDRAWMK_MODEL )
        return;
    model = handle->u.model.model;
    if( !model || model->vertex_count <= 0 || model->original_vertices_x )
        return;
    ToriDraw_ModelCaptureOriginalVertices(model);
}

void
ToriDraw_SceneElementSetModel(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    /*
     * Mounting a model is the last moment it is guaranteed to be at its bind
     * pose, and the first at which the renderer may animate it -- so it is
     * where the bind pose has to exist by. Without originals
     * ToriDraw_ModelAnimateReset silently returns and every keyframe composes
     * with the one before it, which looks like the model inflating into shards
     * rather than like anything missing.
     *
     * The case that reaches here is a model swapped UNDER a running animation
     * without the sequence being re-bound afterwards (a same-tick npc_changetype
     * + npc_anim that re-binds the sequence already playing), so the gate is
     * that the element is ALREADY animated. The opposite order -- model first,
     * animation after -- is covered where the element becomes animated:
     * SetAnimation, SetAnimationSeq and SetSecondaryAnimationSeq each capture.
     * Between the four, an element still cannot hold an unresettable model.
     *
     * The gate is what makes it affordable. Capturing for every element that
     * merely HAS a model meant three mallocs apiece across a whole region --
     * 3.3 MB of bind poses that nothing could ever read, in 58k heap blocks
     * whose per-block overhead the allocation tracker does not even see.
     */
    if( td_scene_element_is_animated(element) )
        td_ensure_bind_pose(&model);

    td_scene_element_assign_model(scene, element_id, model);
}

void
ToriDraw_SceneElementSetAnimation(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_Animation* animation,
    bool primary)
{
    struct ToriDraw_SceneElement* element;
    struct ToriDraw_Animation** slot;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    slot = primary ? &element->animation : &element->secondary_animation;

    if( animation )
    {
        *slot = animation;
        /* Animated from here on, and SetModel declined to capture while it was
         * not. Whatever the model is holding now is its bind pose. */
        td_ensure_bind_pose(&element->model);
        if( !element->dynamic )
        {
            struct ToriDraw_Event* event;
            int old_event_count = scene->event_queue.count;
            td_scene_emit(
                scene,
                TORIDRAW_EVENT_ANIM_LOAD,
                0,
                element_id,
                0,
                0,
                &element->model,
                animation,
                NULL);
            /* Retained renderers keep primary and secondary poses in separate
             * TRSPK pose tracks.  Preserve the track on the load event just as
             * BatchElementAddPose does for explicitly prebaked poses. */
            if( scene->event_queue.count > old_event_count )
            {
                event = &scene->event_queue.events[scene->event_queue.count - 1];
                event->anim_index = primary ? 0 : 1;
            }
        }
    }
    else
    {
        *slot = NULL;
        if( primary )
        {
            element->anim_seq_id = -1;
            element->is_skeletal = false;
            element->skeletal_animation = NULL;
            element->skeletal_play_frames = 0;
            scene->anim_list_dirty = true;
            if( element->model.kind == TORIDRAWMK_MODEL && element->model.u.model.model )
            {
                ToriDraw_ModelAnimateReset(element->model.u.model.model);
                /* The reset restores the AUTHORED bind pose; a model with a
                 * post-resize has to be put back into render scale or it
                 * springs to full size the moment its animation is dropped. */
                ToriDraw_ModelApplyPostTransforms(element->model.u.model.model);
                ToriDraw_ModelSetBoundsCylinder(element->model.u.model.model);
            }
        }
        td_scene_emit(
            scene, TORIDRAW_EVENT_ANIM_UNLOAD, 0, element_id, 0, 0, &element->model, NULL, NULL);
    }
}

void
ToriDraw_SceneElementSetAnimLoop(
    struct ToriDraw_Scene* scene,
    int element_id,
    bool loop)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    if( !td_scene_element_valid(scene, element_id) )
        return;

    element = td_scene_element_ptr(scene, element_id);
    if( element )
        element->anim_loop = loop;
}

void
ToriDraw_SceneElementSetAnimationSeq(
    struct ToriDraw_Scene* scene,
    int element_id,
    int seq_id)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    element->anim_seq_id = seq_id;
    element->anim_frame = 0;
    element->anim_cycle = 0;
    element->animation = NULL;
    element->is_skeletal = false;
    element->skeletal_animation = NULL;
    element->skeletal_play_frames = 0;
    scene->anim_list_dirty = true;

    if( element->model.kind == TORIDRAWMK_MODEL )
    {
        struct ToriDraw_Model* model = element->model.u.model.model;
        ToriDraw_ModelAnimateReset(model);
        /* Capture before the resize: the bind pose every keyframe is applied to
         * is the model at its AUTHORED size (see post_resize). */
        ToriDraw_ModelCaptureOriginalVertices(model);
        ToriDraw_ModelApplyPostTransforms(model);
        ToriDraw_ModelSetBoundsCylinder(model);
    }
}

void
ToriDraw_SceneElementSetSecondaryAnimationSeq(
    struct ToriDraw_Scene* scene,
    int element_id,
    int seq_id)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    element->anim2_seq_id = seq_id;
    element->anim2_frame = 0;
    if( seq_id <= 0 )
        element->secondary_animation = NULL;
    else
        td_ensure_bind_pose(&element->model);
}

void
ToriDraw_SceneElementSetAnimFrames(
    struct ToriDraw_Scene* scene,
    int element_id,
    int primary_frame,
    int secondary_frame)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    element->anim_frame = primary_frame;
    element->anim2_frame = secondary_frame;
}

void
ToriDraw_SceneElementApplyAnimation(
    struct ToriDraw_Scene* scene,
    int element_id,
    bool primary,
    int frame)
{
    struct ToriDraw_SceneElement* element;
    struct ToriDraw_Model* model;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    if( element->model.kind != TORIDRAWMK_MODEL )
        return;
    model = element->model.u.model.model;
    if( !model )
        return;

    if( element->is_skeletal )
    {
        struct ToriDraw_SkeletalAnim* skeletal = element->skeletal_animation;
        /* Skinning needs both halves: the palette and the model's per-vertex
         * bone influences. A skeletal seq bound to a model with no animaya skin
         * (a classic model, or one merged from parts that had none) has nothing
         * to pose — hold the rest pose rather than fault on the missing arrays. */
        if( !skeletal || skeletal->frame_count <= 0 || model->animaya_vertex_count <= 0 ||
            !model->animaya_group_counts || !model->animaya_groups || !model->animaya_scales )
            return;
        if( frame < 0 || frame >= skeletal->frame_count )
            frame = 0;
        ToriDraw_ModelAnimateSkeletal(model, skeletal, frame);
    }
    else
    {
        struct ToriDraw_Animation* animation =
            primary ? element->animation : element->secondary_animation;
        if( !animation || !animation->base || !animation->frames || animation->frame_count <= 0 )
            return;
        if( frame < 0 || frame >= animation->frame_count )
            frame = 0;
        /* A sequence can carry hole frames (frame archive missing or zero
         * translators); hold the rest pose for those instead of asserting. */
        if( animation->frames[frame].length <= 0 )
        {
            ToriDraw_ModelAnimateReset(model);
            ToriDraw_ModelApplyPostTransforms(model);
            ToriDraw_ModelSetBoundsCylinder(model);
            return;
        }
        ToriDraw_ModelAnimateReset(model);

        /* Walkmerge blend: while a primary (action) seq with a walkmerge
         * mask plays and a secondary (walk) track is bound, the secondary
         * keeps driving the masked groups (reference maskAnimate). */
        if( primary && animation->walkmerge && element->secondary_animation &&
            element->secondary_animation->base && element->secondary_animation->frames &&
            element->secondary_animation->frame_count > 0 )
        {
            struct ToriDraw_Animation* second = element->secondary_animation;
            int frame2 = element->anim2_frame;
            if( frame2 < 0 || frame2 >= second->frame_count )
                frame2 = 0;
            if( second->frames[frame2].length > 0 )
            {
                ToriDraw_ModelAnimateFrameMasked(
                    model,
                    animation->base,
                    &animation->frames[frame],
                    &second->frames[frame2],
                    animation->walkmerge);
                return;
            }
        }

        ToriDraw_ModelAnimateFrame(model, animation->base, &animation->frames[frame]);

        /*
         * TORIRS_ANIM_STACK: how far this pose moved the model off its own bind
         * pose, measured absolutely.
         *
         * TORIRS_ANIM_BLOWUP below reports a >2x JUMP in the bounds radius,
         * which answers "what changed" but not "is this size legitimate" -- and
         * its per-element slot is `element_id & 255`, so the radius it compares
         * against can belong to a different element that happens to collide in
         * the low byte. Neither problem matters when the model is 6x its own
         * bind: that is not a jump relative to a neighbour, it is a pose that
         * cannot be right, and the bind pose to compare against is sitting in
         * original_vertices where nothing else can pollute it.
         *
         * A frame is applied to the reset bind pose (see the AnimateReset
         * above), so an oversized RESULT means either the transforms are wrong
         * for this model or the captured "bind" was itself a pose. Printing the
         * model pointer and vertex count separates those: the same element
         * reporting a growing extent for one model pointer is geometry
         * compounding, a bad frame is the same extent every time.
         */
        /*
         * No captured originals at all is the sharpest form of the same fault.
         * ToriDraw_ModelAnimateReset is gated on original_vertices_x, so a model
         * that never captured them silently RETURNS instead of restoring, and
         * the keyframe above composes with the previous frame's output forever.
         * The model does not animate, it accumulates.
         */
        /* All three of these run once per posed element per frame, so the
         * env probes are resolved once rather than per call. */
        static int anim_stack = -1;
        static int anim_blowup = -1;
        static int anim_debug = -1;
        if( anim_stack < 0 )
        {
            anim_stack = getenv("TORIRS_ANIM_STACK") != NULL;
            anim_blowup = getenv("TORIRS_ANIM_BLOWUP") != NULL;
            anim_debug = getenv("TORIRS_ANIM_DEBUG") != NULL;
        }

        if( anim_stack && !model->original_vertices_x &&
            model->vertex_count > 0 )
            fprintf(
                stderr,
                "anim_stack: element=%d seq=%d frame=%d model=%p verts=%d -- NO "
                "captured bind pose, so AnimateReset is a no-op and every frame "
                "composes with the last; this model can only grow\n",
                element_id, element->anim_seq_id, frame, (void*)model,
                model->vertex_count);

        if( anim_stack && model->original_vertices_x &&
            model->original_vertices_y && model->original_vertices_z &&
            model->vertex_count > 0 )
        {
            /* EVERY vertex, not a sample. The failure throws a HANDFUL of
             * vertices a long way -- that is what "stretched shards" is -- so a
             * strided scan reports a healthy span while the bounds cylinder,
             * which does see every vertex, reports six times more. Sampling
             * here hid the defect and made the model look innocent. */
            int const n = model->vertex_count;
            int bind_lo = model->original_vertices_y[0];
            int bind_hi = bind_lo;
            int pose_lo = model->vertices_y[0];
            int pose_hi = pose_lo;
            int worst_v = 0, worst_d = 0;
            for( int i = 0; i < n; i++ )
            {
                int b = model->original_vertices_y[i];
                int p = model->vertices_y[i];
                int d = p - b;
                if( d < 0 ) d = -d;
                if( b < bind_lo ) bind_lo = b;
                if( b > bind_hi ) bind_hi = b;
                if( p < pose_lo ) pose_lo = p;
                if( p > pose_hi ) pose_hi = p;
                if( d > worst_d ) { worst_d = d; worst_v = i; }
            }
            /* The bind pose is the AUTHORED size and the pose has already had
             * the model's post-resize applied (see post_resize), so the two
             * spans are in different scales -- a half-size npc would never trip
             * the ratio below and a double-size one would trip it on every
             * healthy frame. Compare the bind span the pose was actually built
             * from. */
            int bind_span = bind_hi - bind_lo;
            int const pose_span = pose_hi - pose_lo;
            if( model->post_resize )
                bind_span = bind_span * model->post_resize_height / 128;
            /* Her wake sequence legitimately reaches ~5x the bind span (she
             * rears up out of a coiled rest pose), which is why this is 8x and
             * not the 3x that looked generous on paper -- 3x reported every
             * healthy frame of every sequence. The broken pose is ~7.5x the
             * legitimate one, so 8x separates them cleanly. */
            if( bind_span > 0 && pose_span > bind_span * 8 )
                fprintf(
                    stderr,
                    "anim_stack: element=%d seq=%d frame=%d model=%p verts=%d -- "
                    "posed y-span %d vs bind span %d (bind %d..%d, posed %d..%d); "
                    "furthest vertex %d moved %d\n",
                    element_id, element->anim_seq_id, frame, (void*)model, n,
                    pose_span, bind_span, bind_lo, bind_hi, pose_lo, pose_hi,
                    worst_v, worst_d);
        }


        /*
         * TORIRS_ANIM_BLOWUP: name the sequence and frame whose transforms blow
         * a model's geometry out, and nothing else.
         *
         * This is the question left over once the magnitude cliffs are closed:
         * the model no longer vanishes, but a pose that reaches radius 43,425
         * is still wrong, and the only thing worth knowing is WHICH keyframe
         * did it. Edge-triggered on a large jump in the bounds radius, so a
         * whole session produces a handful of lines instead of one per pose --
         * TORIRS_ANIM_DEBUG's per-frame firehose is why this was never legible.
         */
        if( anim_blowup && model->has_bounds_cylinder )
        {
            /* Per ELEMENT. A single shared `last` compares one model's radius
             * against whatever was posed immediately before it, which fires on
             * every scene that draws two differently-sized models -- noise that
             * says nothing about any animation. */
            static int last_radius[256];
            static bool last_seen[256];
            int const key = element_id & 255;
            int const r = model->bounds_cylinder.radius;
            int const prev = last_seen[key] ? last_radius[key] : -1;

            if( prev >= 0 && (r > prev * 2 + 64 || prev > r * 2 + 64) )
                fprintf(
                    stderr,
                    "anim_blowup: element=%d seq=%d frame=%d radius %d -> %d "
                    "(min_y=%d max_y=%d bias=%d) -- this keyframe changed the "
                    "model's extent by more than 2x\n",
                    element_id, element->anim_seq_id, frame, prev, r,
                    model->bounds_cylinder.min_y, model->bounds_cylinder.max_y,
                    model->bounds_cylinder.min_z_depth_any_rotation);
            last_radius[key] = r;
            last_seen[key] = true;
        }

        /* TORIRS_ANIM_DEBUG: one line per (element, frame) with the resulting
         * bounds cylinder, to catch a keyframe whose decoded transforms blow
         * the model's geometry out to a radius the camera/culling can't
         * handle. See docs/rs2012_qbd_wakeup. */
        if( anim_debug && model->has_bounds_cylinder )
            fprintf(
                stderr,
                "anim: element=%d primary=%d seq=%d frame=%d verts=%d radius=%d min_y=%d "
                "max_y=%d\n",
                element_id,
                (int)primary,
                element->anim_seq_id,
                frame,
                model->vertex_count,
                model->bounds_cylinder.radius,
                model->bounds_cylinder.min_y,
                model->bounds_cylinder.max_y);
    }
}

void
ToriDraw_SceneElementSetPosition(
    struct ToriDraw_Scene* scene,
    int element_id,
    int x,
    int y,
    int z,
    int yaw)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    element->world_position.x = x;
    element->world_position.y = y;
    element->world_position.z = z;
    element->world_position.yaw = ToriDraw_NormalizeAngle(yaw);
    element->world_position.pitch = 0;
    element->world_position.roll = 0;

    if( scene->batch_building && element->pending_batch_add )
    {
        td_scene_emit(
            scene,
            TORIDRAW_EVENT_BATCH_MODEL_ADD,
            scene->current_batch_id,
            element_id,
            0,
            0,
            &element->model,
            NULL,
            NULL);
        element->pending_batch_add = false;
    }
}

void
ToriDraw_SceneElementSetPositionPitchYaw(
    struct ToriDraw_Scene* scene,
    int element_id,
    int x,
    int y,
    int z,
    int pitch,
    int yaw)
{
    struct ToriDraw_SceneElement* element;

    assert(scene);
    assert(td_scene_element_valid(scene, element_id));

    element = td_scene_element_ptr(scene, element_id);
    assert(element);

    element->world_position.x = x;
    element->world_position.y = y;
    element->world_position.z = z;
    element->world_position.pitch = ToriDraw_NormalizeAngle(pitch);
    element->world_position.yaw = ToriDraw_NormalizeAngle(yaw);
    element->world_position.roll = 0;

    if( scene->batch_building && element->pending_batch_add )
    {
        td_scene_emit(
            scene,
            TORIDRAW_EVENT_BATCH_MODEL_ADD,
            scene->current_batch_id,
            element_id,
            0,
            0,
            &element->model,
            NULL,
            NULL);
        element->pending_batch_add = false;
    }
}

void
ToriDraw_SceneBatchBegin(struct ToriDraw_Scene* scene)
{
    assert(scene);
    assert(!scene->batch_building);

    scene->batch_building = true;
    scene->current_batch_id = scene->next_batch_id++;
    scene->current_batch_element_count = 0;

    td_scene_emit(
        scene, TORIDRAW_EVENT_BATCH_BEGIN, scene->current_batch_id, 0, 0, 0, NULL, NULL, NULL);
}

struct ToriDraw_SceneBatchElementHandle
ToriDraw_SceneBatchAddElement(struct ToriDraw_Scene* scene)
{
    struct ToriDraw_SceneBatchElementHandle invalid;
    struct ToriDraw_SceneBatchElementHandle handle;
    int element_id;

    TD_SCENE_BATCH_ELEMENT_HANDLE_INVALID(invalid, scene);

    assert(scene);
    assert(scene->batch_building);

    element_id = td_scene_allocate_element_id(scene, TORIDRAW_SCENE_POOL_STATIC);
    if( element_id < 0 )
        return invalid;

    scene->current_batch_element_count++;

    td_scene_emit(
        scene,
        TORIDRAW_EVENT_BATCH_MODEL_ADD,
        scene->current_batch_id,
        element_id,
        0,
        0,
        NULL,
        NULL,
        NULL);

    handle.scene = scene;
    handle.batch_id = scene->current_batch_id;
    handle.id = element_id;
    return handle;
}

void
ToriDraw_SceneBatchEnd(struct ToriDraw_Scene* scene)
{
    int i;

    assert(scene);
    assert(scene->batch_building);

    for( i = scene->elements.head; i != TORIDRAW_INTRUSIVE_NIL; i = scene->elements.nodes[i].next )
    {
        struct ToriDraw_SceneElement* element = td_scene_element_ptr(scene, i);
        if( !element || !element->pending_batch_add )
            continue;
        td_scene_emit(
            scene,
            TORIDRAW_EVENT_BATCH_MODEL_ADD,
            scene->current_batch_id,
            i,
            0,
            0,
            &element->model,
            NULL,
            NULL);
        element->pending_batch_add = false;
    }

    td_scene_emit(
        scene, TORIDRAW_EVENT_BATCH_END, scene->current_batch_id, 0, 0, 0, NULL, NULL, NULL);

    scene->batch_building = false;
    scene->current_batch_element_count = 0;
}

void
ToriDraw_SceneBatchClear(
    struct ToriDraw_Scene* scene,
    int batch_id)
{
    assert(scene);
    td_scene_emit(scene, TORIDRAW_EVENT_BATCH_CLEAR, batch_id, 0, 0, 0, NULL, NULL, NULL);
}

void
ToriDraw_SceneBatchElementAddPose(
    struct ToriDraw_Scene* scene,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle baked)
{
    struct ToriDraw_Event* event;
    int old_event_count;

    assert(scene);
    assert(scene->batch_building);
    assert(baked.kind == TORIDRAWMK_MODEL);
    assert(td_scene_element_valid(scene, element_id));

    old_event_count = scene->event_queue.count;
    td_scene_emit(
        scene,
        TORIDRAW_EVENT_BATCH_ANIM_ADD,
        scene->current_batch_id,
        element_id,
        pose_id,
        0,
        &baked,
        NULL,
        NULL);

    /* Back-patch anim_index onto the event we just pushed. */
    if( scene->event_queue.count > old_event_count )
    {
        event = &scene->event_queue.events[scene->event_queue.count - 1];
        event->anim_index = anim_index;
    }

    if( baked.u.model.model )
        td_scene_retain_pending_pose(scene, baked.u.model.model);
}

struct ToriDraw_EventQueue*
ToriDraw_SceneEvents(struct ToriDraw_Scene* scene)
{
    assert(scene);
    return &scene->event_queue;
}

void
ToriDraw_SceneSpritesReemitLoads(struct ToriDraw_Scene* scene)
{
    assert(scene);
    assert(scene->sprites_hmap);

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(scene->sprites_hmap);
    struct MapEntry_Sprite* entry = NULL;
    while( (entry = (struct MapEntry_Sprite*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->sprites && entry->count > 0 )
        {
            td_scene_emit_sprite(
                scene, TORIDRAW_EVENT_SPRITE_LOAD, entry->id, entry->sprites, entry->count);
        }
    }
    ToriDraw_MapIterFree(iter);
}

void
ToriDraw_SceneFontsReemitLoads(struct ToriDraw_Scene* scene)
{
    assert(scene);
    assert(scene->fonts_hmap);

    struct ToriDraw_MapIter* iter = ToriDraw_MapIterNew(scene->fonts_hmap);
    struct MapEntry_Font* entry = NULL;
    while( (entry = (struct MapEntry_Font*)ToriDraw_MapIterNext(iter)) )
    {
        if( entry->font )
            td_scene_emit_font(scene, TORIDRAW_EVENT_FONT_LOAD, entry->id, entry->font);
    }
    ToriDraw_MapIterFree(iter);
}

void
ToriDraw_SceneFrameEnd(struct ToriDraw_Scene* scene)
{
    for( int i = 0; i < scene->pending_pose_count; i++ )
    {
        if( scene->pending_poses[i].model )
            ToriDraw_ModelFree(scene->pending_poses[i].model);
    }
    scene->pending_pose_count = 0;
    scene->event_queue.count = 0;
}
