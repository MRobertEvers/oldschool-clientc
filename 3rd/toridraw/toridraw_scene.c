#include "toridraw_scene.h"

#include "toridraw_animation.h"
#include "toridraw_font.h"
#include "toridraw_map.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
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
    if( !new_buffer )
        return;

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
    return (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListGet(&scene->elements, element_id);
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

    if( scene->elements.free_head != TORIDRAW_INTRUSIVE_NIL )
        element = (struct ToriDraw_SceneElement*)ToriDraw_IntrusiveListGet(
            &scene->elements, scene->elements.free_head);
    else
    {
        if( scene->elements.count >= TORIDRAW_SCENE_MAX_ELEMENTS )
            return -1;
        element = calloc(1, sizeof(struct ToriDraw_SceneElement));
        if( !element )
            return -1;
    }

    td_scene_reset_element(element);

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

static void
td_scene_dispose_element_model(struct ToriDraw_SceneElement* element)
{
    assert(element);
    if( element->model.kind == TORIDRAWMK_MODEL && element->model.u.model.model )
        ToriDraw_ModelFree(element->model.u.model.model);
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
                element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
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
        if( entry->model.kind == TORIDRAWMK_MODEL )
            ToriDraw_ModelFree(entry->model.u.model.model);
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
    if( !scene )
        return;

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
    if( !sound )
    {
        free(samples);
        return NULL;
    }
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
    return ToriDraw_SceneModelGet(scene, model_id).kind == TORIDRAWMK_MODEL;
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

    return entry->model;
}

void
ToriDraw_SceneModelsClearAll(struct ToriDraw_Scene* scene)
{
    assert(scene);
    assert(scene->models_hmap);

    td_scene_free_models_map(scene->models_hmap);
    td_scene_map_reset(&scene->models_hmap, sizeof(struct MapEntry_ToriModel), 1024);
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

    if( entry->animation )
        ToriDraw_AnimationFree(entry->animation);

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
                element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
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
            element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
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
                element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
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
                element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
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
        element && element->model.kind == TORIDRAWMK_MODEL ? &element->model : NULL,
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
                if( !grown )
                {
                    /* Out of memory: report empty rather than a truncated list,
                     * and stay dirty so the next call retries. */
                    *out_count = 0;
                    return NULL;
                }
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
    assert(model.kind == TORIDRAWMK_MODEL);
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

void
ToriDraw_SceneElementSetModel(
    struct ToriDraw_Scene* scene,
    int element_id,
    struct ToriDraw_ModelHandle model)
{
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
        ToriDraw_ModelSetBoundsCylinder(model);
        ToriDraw_ModelCaptureOriginalVertices(model);
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

        /* TORIRS_ANIM_DEBUG: one line per (element, frame) with the resulting
         * bounds cylinder, to catch a keyframe whose decoded transforms blow
         * the model's geometry out to a radius the camera/culling can't
         * handle. See docs/rs2012_qbd_wakeup. */
        if( getenv("TORIRS_ANIM_DEBUG") && model->bounds_cylinder )
            fprintf(
                stderr,
                "anim: element=%d primary=%d seq=%d frame=%d verts=%d radius=%d min_y=%d "
                "max_y=%d\n",
                element_id,
                (int)primary,
                element->anim_seq_id,
                frame,
                model->vertex_count,
                model->bounds_cylinder->radius,
                model->bounds_cylinder->min_y,
                model->bounds_cylinder->max_y);
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
