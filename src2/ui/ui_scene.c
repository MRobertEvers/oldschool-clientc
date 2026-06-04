#include "ui_scene.h"

#include <stdlib.h>
#include <string.h>

static void
ui_scene_push_event(
    struct UIScene* scene,
    enum UISceneEventKind kind,
    int element_id)
{
    uiscene_eventqueue_push(
        &scene->events,
        &(struct UISceneEvent){
            .kind = kind,
            .element_id = element_id,
        });
}

struct UIScene*
ui_scene_new(int capacity)
{
    if( capacity <= 0 )
        capacity = 256;

    struct UIScene* scene = calloc(1, sizeof(struct UIScene));
    if( !scene )
        return NULL;

    scene->elements = calloc((size_t)capacity, sizeof(struct UISceneElement));
    if( !scene->elements )
    {
        free(scene);
        return NULL;
    }
    scene->elements_count = capacity;

    for( int i = 0; i < capacity; i++ )
    {
        scene->elements[i].id = i;
        scene->elements[i].active = false;
        if( i < capacity - 1 )
            scene->elements[i].next = &scene->elements[i + 1];
        if( i > 0 )
            scene->elements[i].prev = &scene->elements[i - 1];
    }
    scene->free_list = &scene->elements[0];
    scene->free_len = capacity;

    return scene;
}

// static void
// ui_scene_free_fonts(struct UIScene* scene)
// {
//     if( !scene )
//         return;
//     for( int i = 0; i < scene->font_count; i++ )
//     {
//         if( scene->fonts[i].pixfont )
//             cache_dat_pixfont_free(scene->fonts[i].pixfont);
//         scene->fonts[i].pixfont = NULL;
//     }
//     scene->font_count = 0;
// }

void
ui_scene_free(struct UIScene* scene)
{
    if( !scene )
        return;

    // ui_scene_free_fonts(scene);

    for( int i = 0; i < scene->elements_count; i++ )
    {
        struct UISceneElement* el = &scene->elements[i];
        if( !el->toridraw_sprites || el->toridraw_sprites_borrowed )
            continue;
        for( int j = 0; j < el->toridraw_sprites_count; j++ )
        {
            if( el->toridraw_sprites[j] )
                toridraw_sprite_free(el->toridraw_sprites[j]);
        }
        free(el->toridraw_sprites);
        el->toridraw_sprites = NULL;
        el->toridraw_sprites_count = 0;
    }

    free(scene->elements);
    free(scene);
}

static struct UISceneElement*
ui_scene_take_free(struct UIScene* scene)
{
    if( !scene || !scene->free_list )
        return NULL;

    struct UISceneElement* el = scene->free_list;
    scene->free_list = el->next;
    scene->free_len--;
    el->next = scene->active_list;
    el->prev = NULL;
    if( scene->active_list )
        scene->active_list->prev = el;
    scene->active_list = el;
    scene->active_len++;
    el->active = true;
    return el;
}

int
ui_scene_element_acquire(struct UIScene* scene)
{
    return ui_scene_element_acquire_with_sprites(scene, NULL, 0, false, NULL);
}

int
ui_scene_element_acquire_with_sprites(
    struct UIScene* scene,
    struct ToriDraw_Sprite** sprites,
    int sprites_count,
    bool borrowed,
    const char* name)
{
    struct UISceneElement* el = ui_scene_take_free(scene);
    if( !el )
        return -1;

    el->toridraw_sprites = sprites;
    el->toridraw_sprites_count = sprites_count;
    el->toridraw_sprites_borrowed = borrowed;
    if( name )
    {
        strncpy(el->name, name, sizeof(el->name) - 1);
        el->name[sizeof(el->name) - 1] = '\0';
    }
    else
        el->name[0] = '\0';

    ui_scene_push_event(scene, UISCENE_EVENT_ELEMENT_ACQUIRED, el->id);
    return el->id;
}

struct UISceneElement*
ui_scene_element_at(
    struct UIScene* scene,
    int element_id)
{
    if( !scene || element_id < 0 || element_id >= scene->elements_count )
        return NULL;
    return &scene->elements[element_id];
}

struct UISceneEventQueue*
ui_scene_get_event_queue(struct UIScene* scene)
{
    if( !scene )
        return NULL;
    return &scene->events;
}

int
ui_scene_font_find_id(
    struct UIScene* scene,
    const char* name)
{
    if( !scene || !name || !name[0] )
        return -1;
    for( int i = 0; i < scene->font_count; i++ )
    {
        if( strcmp(scene->fonts[i].name, name) == 0 )
            return i;
    }
    return -1;
}

// struct CacheDatPixfont*
// ui_scene_font_get(
//     struct UIScene* scene,
//     int font_id)
// {
//     if( !scene || font_id < 0 || font_id >= scene->font_count )
//         return NULL;
//     return scene->fonts[font_id].pixfont;
// }

// int
// ui_scene_font_add(
//     struct UIScene* scene,
//     const char* name,
//     struct CacheDatPixfont* pixfont)
// {
//     if( !scene || !name || !pixfont )
//         return -1;
//     if( scene->font_count >= UI_SCENE_MAX_FONTS )
//         return -1;

//     int existing = ui_scene_font_find_id(scene, name);
//     if( existing >= 0 )
//     {
//         if( scene->fonts[existing].pixfont )
//             cache_dat_pixfont_free(scene->fonts[existing].pixfont);
//         scene->fonts[existing].pixfont = pixfont;
//         return existing;
//     }

//     int idx = scene->font_count++;
//     strncpy(scene->fonts[idx].name, name, sizeof(scene->fonts[idx].name) - 1);
//     scene->fonts[idx].name[sizeof(scene->fonts[idx].name) - 1] = '\0';
//     scene->fonts[idx].pixfont = pixfont;
//     return idx;
// }

// static void
// ui_scene_load_font_file(
//     struct UIScene* scene,
//     struct FileListDat* filelist,
//     int index_file_idx,
//     const char* dat_name,
//     const char* font_name)
// {
//     int data_file_idx = filelist_dat_find_file_by_name(filelist, dat_name);
//     if( data_file_idx < 0 || index_file_idx < 0 )
//         return;

//     struct CacheDatPixfont* pixfont = cache_dat_pixfont_new_decode(
//         filelist->files[data_file_idx],
//         filelist->file_sizes[data_file_idx],
//         filelist->files[index_file_idx],
//         filelist->file_sizes[index_file_idx]);
//     if( !pixfont )
//         return;

//     ui_scene_font_add(scene, font_name, pixfont);
// }

// void
// ui_scene_load_fonts_from_title_archive(
//     struct UIScene* scene,
//     struct FileListDat* filelist)
// {
//     if( !scene || !filelist )
//         return;

//     int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
//     if( index_file_idx < 0 )
//         return;

//     ui_scene_load_font_file(scene, filelist, index_file_idx, "b12.dat", "b12");
//     ui_scene_load_font_file(scene, filelist, index_file_idx, "p12.dat", "p12");
//     ui_scene_load_font_file(scene, filelist, index_file_idx, "p11.dat", "p11");
//     ui_scene_load_font_file(scene, filelist, index_file_idx, "q8.dat", "q8");
// }
