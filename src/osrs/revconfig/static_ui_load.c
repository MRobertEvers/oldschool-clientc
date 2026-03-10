#include "static_ui_load.h"

#include "bmp.h"
#include "graphics/dash.h"
#include "osrs/dash_utils.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"

#include <assert.h>

struct SpriteEntry
{
    char name[64]; // Key must be first field and fixed size for DashMap
    struct DashSprite** sprites;
    int count;
    int id;
};

struct SpriteLoad
{
    char name[64];
    char table[64];
    char archive[64];
    char container[64];
    char index_filename[64];
    char data_filename[64];
    char format[16];
    int atlas_index;
    int atlas_count;

    char transforms[5][32];
    int transform_count;
};

enum LoadKind
{
    LOAD_KIND_NONE,
    LOAD_KIND_SPRITE,
};

struct CurrentLoad
{
    enum LoadKind kind;
    union
    {
        struct SpriteLoad _sprite;
    };
};

static struct DashSprite*
load_sprite_pix8(
    struct FileListDat* filelist,
    int data_file_idx,
    int index_file_idx,
    int sprite_idx)
{
    struct CacheDatPix8Palette* pix8_palette = NULL;
    struct DashSprite* sprite = NULL;

    pix8_palette = cache_dat_pix8_palette_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);

    sprite = dashsprite_new_from_cache_pix8_palette(pix8_palette);
    cache_dat_pix8_palette_free(pix8_palette);

    return sprite;
}

static struct DashSprite*
load_sprite_pix32(
    struct FileListDat* filelist,
    int data_file_idx,
    int index_file_idx,
    int sprite_idx)
{
    struct CacheDatPix32* pix32 = NULL;
    struct DashSprite* sprite = NULL;

    pix32 = cache_dat_pix32_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( !pix32 )
    {
        return NULL;
    }
    sprite = dashsprite_new_from_cache_pix32(pix32);
    cache_dat_pix32_free(pix32);
    return sprite;
}

static void
load_sprite(
    struct SpriteLoad* load,
    struct DashMap* sprite_hmap,
    struct UIScene* static_ui,
    struct BuildCacheDat* buildcachedat)
{
    assert(
        buildcachedat->cfg_media_jagfile &&
        "buildcachedat must have media_filelist to load sprites");

    struct FileListDat* filelist = buildcachedat->cfg_media_jagfile;
    int index_file_idx = filelist_dat_find_file_by_name(filelist, load->index_filename);
    int data_file_idx = filelist_dat_find_file_by_name(filelist, load->data_filename);
    if( index_file_idx == -1 || data_file_idx == -1 )
    {
        assert(0 && "Failed to find index or data file in filelist for sprite");
        return;
    }

    struct DashSprite** sprites = NULL;

    int count = load->atlas_index < 0 ? load->atlas_count : 1;
    sprites = malloc(count * sizeof(struct DashSprite*));
    if( !sprites )
    {
        assert(0 && "Failed to allocate sprites array");
        return;
    }

    for( int atlas_index = 0; atlas_index < count; atlas_index++ )
    {
        if( strcmp(load->format, "pix8") == 0 )
        {
            sprites[atlas_index] =
                load_sprite_pix8(filelist, data_file_idx, index_file_idx, atlas_index);
        }
        else if( strcmp(load->format, "pix32") == 0 )
        {
            sprites[atlas_index] =
                load_sprite_pix32(filelist, data_file_idx, index_file_idx, atlas_index);
        }
        else
        {
            assert(0 && "Unknown sprite format");
        }

        if( !sprites[atlas_index] )
        {
            // Ignore failed loads?
            continue;
        }

        for( int i = 0; i < 5; i++ )
        {
            if( load->transforms[i][0] != '\0' )
            {
                if( strcmp(load->transforms[i], "flip_h") == 0 )
                    dashsprite_flip_horizontal(sprites[atlas_index]);
                else if( strcmp(load->transforms[i], "flip_v") == 0 )
                    dashsprite_flip_vertical(sprites[atlas_index]);
                else
                    assert(0 && "Unknown transform");
            }
        }

        char filename[128] = { 0 };
        snprintf(filename, sizeof(filename), "build/%s%d.bmp", load->name, atlas_index);
        bmp_write_file(
            filename,
            sprites[atlas_index]->pixels_argb,
            sprites[atlas_index]->width,
            sprites[atlas_index]->height);
    }

    int element_id = uiscene_element_acquire(static_ui, -1);
    struct UISceneElement* element = uiscene_element_at(static_ui, element_id);
    element->dash_sprites = sprites;
    strncpy(element->name, load->name, sizeof(element->name) - 1);

    struct SpriteEntry* sprite_entry = dashmap_search(sprite_hmap, load->name, DASHMAP_INSERT);

    assert(sprite_entry && "Sprite must be inserted into hmap");
    sprite_entry->sprites = sprites;
    sprite_entry->id = element_id;
    sprite_entry->count = count;
    strncpy(sprite_entry->name, load->name, sizeof(sprite_entry->name) - 1);
};

static uint32_t
load_kind(const char* str)
{
    if( strcmp(str, "sprite") == 0 )
        return LOAD_KIND_SPRITE;
    return LOAD_KIND_NONE;
}

static void
load_item(
    struct CurrentLoad* load,
    struct DashMap* sprite_hmap,
    struct UIScene* static_ui,
    struct BuildCacheDat* buildcachedat)
{
    switch( load->kind )
    {
    case LOAD_KIND_SPRITE:
        load_sprite(&load->_sprite, sprite_hmap, static_ui, buildcachedat);
        break;
    }
}

void
static_ui_from_revconfig_buildcachedat(
    struct UIScene* static_ui,
    struct BuildCacheDat* buildcachedat,
    struct RevConfigBuffer* revconfig_buffer)
{
    struct CurrentLoad load = { 0 };

    struct DashMapConfig config = {
        .buffer = malloc(1024 * sizeof(struct SpriteEntry)),
        .buffer_size = 1024 * sizeof(struct SpriteEntry),
        .key_size = 64, // Max sprite name length
        .entry_size = sizeof(struct SpriteEntry),
    };
    struct DashMap* sprite_hmap = dashmap_new(&config, 0);

    for( uint32_t i = 0; i < revconfig_buffer->field_count; i++ )
    {
        struct RevConfigField* field = &revconfig_buffer->fields[i];
        switch( field->kind )
        {
        case RCFIELD_ITEMTYPE:
            load.kind = load_kind(field->value);
            break;
        case RCFIELD_ITEMNAME:
            strncpy(load._sprite.name, field->value, sizeof(load._sprite.name) - 1);
            break;
        case RCFIELD_ITEMDONE:
            load_item(&load, sprite_hmap, static_ui, buildcachedat);
            load.kind = LOAD_KIND_NONE;
            memset(&load._sprite, 0, sizeof(load._sprite));
            break;
        case RCFIELD_CACHE_TABLE:
            strncpy(load._sprite.table, field->value, sizeof(load._sprite.table) - 1);
            break;
        case RCFIELD_CACHE_ARCHIVE:
            strncpy(load._sprite.archive, field->value, sizeof(load._sprite.archive) - 1);
            break;
        case RCFIELD_CACHE_CONTAINER:
            strncpy(load._sprite.container, field->value, sizeof(load._sprite.container) - 1);
            break;
        case RCFIELD_CACHE_INDEX_FILENAME:
            strncpy(
                load._sprite.index_filename, field->value, sizeof(load._sprite.index_filename) - 1);
            break;
        case RCFIELD_CACHE_DATA_FILENAME:
            strncpy(
                load._sprite.data_filename, field->value, sizeof(load._sprite.data_filename) - 1);
            break;
        case RCFIELD_CACHE_FORMAT:
            strncpy(load._sprite.format, field->value, sizeof(load._sprite.format) - 1);
            break;
        case RCFIELD_CACHE_ATLAS_INDEX:
            load._sprite.atlas_index = atoi(field->value);
            break;
        case RCFIELD_CACHE_ATLAS_COUNT:
            load._sprite.atlas_count = atoi(field->value);
            load._sprite.atlas_index = -1;
            break;
        case RCFIELD_CACHE_TRANSFORM:
            if( load._sprite.transform_count < 5 )
            {
                strncpy(
                    load._sprite.transforms[load._sprite.transform_count],
                    field->value,
                    sizeof(load._sprite.transforms[load._sprite.transform_count]) - 1);
                load._sprite.transform_count++;
            }
            else
            {
                assert(0 && "Too many transforms specified for sprite");
            }
            break;
        }
    }
}