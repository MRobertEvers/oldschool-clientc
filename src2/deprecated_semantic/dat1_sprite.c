#include "dat1_sprite.h"

#include "../buildcache/dat1_buildcache.h"
#include "../ui/ui_scene.h"
#include "gamecache/toridraw_cachesprite.h"
#include "graphics/dashmap.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "toridraw/toridraw_sprite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

static bool
sprite_resolve_archive(
    const char* table,
    const char* archive,
    int* out_table_id,
    int* out_archive_id)
{
    if( !table || !archive || !out_table_id || !out_archive_id )
        return false;
    if( strcmp(table, "configs") == 0 )
    {
        *out_table_id = CACHE_DAT_CONFIGS;
        if( strcmp(archive, "media") == 0 )
        {
            *out_archive_id = CONFIG_DAT_MEDIA_2D_GRAPHICS;
            return true;
        }
    }
    return false;
}

static struct ToriDraw_Sprite*
sprite_decode_from_filelist(
    struct FileListDat* filelist,
    const struct Dat1_Sprite* s,
    int atlas_index)
{
    if( !filelist || !s )
        return NULL;

    int index_file_idx = filelist_dat_find_file_by_name(filelist, s->index_filename);
    int data_file_idx = filelist_dat_find_file_by_name(filelist, s->data_filename);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    if( strcmp(s->format, "pix8") == 0 )
    {
        struct CacheDatPix8Palette* pix8 = cache_dat_pix8_palette_new(
            filelist->files[data_file_idx],
            filelist->file_sizes[data_file_idx],
            filelist->files[index_file_idx],
            filelist->file_sizes[index_file_idx],
            atlas_index);
        if( !pix8 )
            return NULL;
        struct ToriDraw_Sprite* sprite = toridraw_sprite_new_from_cache_pix8_palette(pix8);
        cache_dat_pix8_palette_free(pix8);
        return sprite;
    }

    if( strcmp(s->format, "pix32") == 0 )
    {
        struct CacheDatPix32* pix32 = cache_dat_pix32_new(
            filelist->files[data_file_idx],
            filelist->file_sizes[data_file_idx],
            filelist->files[index_file_idx],
            filelist->file_sizes[index_file_idx],
            atlas_index);
        if( !pix32 )
            return NULL;
        struct ToriDraw_Sprite* sprite = toridraw_sprite_new_from_cache_pix32(pix32);
        cache_dat_pix32_free(pix32);
        return sprite;
    }

    return NULL;
}

static struct ToriDraw_Sprite*
sprite_placeholder(
    int width,
    int height)
{
    int w = width > 0 ? width : 32;
    int h = height > 0 ? height : 32;
    uint32_t* pixels = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    if( !pixels )
        return NULL;
    for( int i = 0; i < w * h; i++ )
        pixels[i] = 0xFFFF00FFu;
    return toridraw_sprite_new_from_argb_owned(pixels, w, h);
}

/* ------------------------------------------------------------------ */
/* dat1_sprite_decode_media_ref (shared: used by Dat1_Interface.INSPECT) */
/* ------------------------------------------------------------------ */

struct ToriDraw_Sprite*
dat1_sprite_decode_media_ref(
    struct Dat1BuildCache* buildcache,
    const char* sprite_ref)
{
    struct FileListDat* filelist = dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
    if( !filelist || !sprite_ref || !sprite_ref[0] )
        return NULL;

    char filename_buf[256];
    int sprite_idx = 0;
    size_t len;

    if( sscanf(sprite_ref, "%255[^,],%d", filename_buf, &sprite_idx) != 2 )
    {
        strncpy(filename_buf, sprite_ref, sizeof(filename_buf) - 1);
        filename_buf[sizeof(filename_buf) - 1] = '\0';
        sprite_idx = 0;
    }

    len = strlen(filename_buf);
    if( len + 5 <= sizeof(filename_buf) &&
        (len < 4 || strcmp(filename_buf + len - 4, ".dat") != 0) )
    {
        memcpy(filename_buf + len, ".dat", 4);
        filename_buf[len + 4] = '\0';
    }

    int index_file_idx = filelist_dat_find_file_by_name(filelist, "index.dat");
    int data_file_idx = filelist_dat_find_file_by_name(filelist, filename_buf);
    if( index_file_idx < 0 || data_file_idx < 0 )
        return NULL;

    struct CacheDatPix32* pix32 = cache_dat_pix32_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( pix32 )
    {
        struct ToriDraw_Sprite* sprite = toridraw_sprite_new_from_cache_pix32(pix32);
        cache_dat_pix32_free(pix32);
        if( sprite )
            return sprite;
    }

    struct CacheDatPix8Palette* pix8 = cache_dat_pix8_palette_new(
        filelist->files[data_file_idx],
        filelist->file_sizes[data_file_idx],
        filelist->files[index_file_idx],
        filelist->file_sizes[index_file_idx],
        sprite_idx);
    if( !pix8 )
        return NULL;
    struct ToriDraw_Sprite* sprite = toridraw_sprite_new_from_cache_pix8_palette(pix8);
    cache_dat_pix8_palette_free(pix8);
    return sprite;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
dat1_sprite_init(struct Dat1_Sprite* s)
{
    if( !s )
        return;
    memset(s, 0, sizeof(*s));
    s->state = DAT1_STATE_NEED_ARCHIVE;
    s->scene_id = -1;
}

void
dat1_sprite_step(
    struct Dat1_Sprite* s,
    struct Dat1BuildCache* buildcache,
    struct UIScene* scene,
    struct DashMap* sprite_map)
{
    if( !s || !buildcache || !scene || !sprite_map )
        return;

    switch( s->state )
    {
    case DAT1_STATE_NEED_ARCHIVE:
    {
        int table_id = 0;
        int archive_id = 0;
        if( !sprite_resolve_archive(s->table, s->archive, &table_id, &archive_id) )
        {
            printf(
                "dat1_sprite: unknown archive '%s/%s' for sprite '%s'\n",
                s->table,
                s->archive,
                s->name);
            s->state = DAT1_STATE_FAILED;
            return;
        }

        /* If the media jagfile is already in buildcache, skip straight to CONVERT. */
        if( table_id == CACHE_DAT_CONFIGS && archive_id == CONFIG_DAT_MEDIA_2D_GRAPHICS &&
            dat1_buildcache_get_media_2d_graphics_jagfile(buildcache) )
        {
            s->state = DAT1_STATE_CONVERT;
            break; /* re-enter switch */
        }

        s->waiting[0].table_id = table_id;
        s->waiting[0].archive_id = archive_id;
        s->waiting[0].flags = 0;
        s->waiting[0].resolved = false;
        s->waiting_count = 1;
        s->state = DAT1_STATE_WAIT;
        return;
    }

    case DAT1_STATE_WAIT:
        if( !s->waiting[0].resolved )
            return;
        s->state = DAT1_STATE_CONVERT;
        /* fall through */

    case DAT1_STATE_CONVERT:
    {
        struct FileListDat* filelist = dat1_buildcache_get_media_2d_graphics_jagfile(buildcache);
        if( !filelist )
        {
            s->state = DAT1_STATE_FAILED;
            return;
        }

        int start_atlas = s->atlas_use_count ? 0 : s->atlas_index;
        int count = (s->atlas_use_count && s->atlas_count > 0) ? s->atlas_count : 1;

        struct ToriDraw_Sprite** sprites = calloc((size_t)count, sizeof(struct ToriDraw_Sprite*));
        if( !sprites )
        {
            s->state = DAT1_STATE_FAILED;
            return;
        }

        int loaded = 0;
        for( int j = 0; j < count; j++ )
        {
            sprites[j] = sprite_decode_from_filelist(filelist, s, start_atlas + j);
            if( !sprites[j] )
                sprites[j] = sprite_placeholder(32, 32);
            if( sprites[j] )
                loaded++;
        }

        if( loaded <= 0 )
        {
            free(sprites);
            printf("dat1_sprite: failed to load any frames for '%s'\n", s->name);
            s->state = DAT1_STATE_FAILED;
            return;
        }

        int scene_id = ui_scene_element_acquire_with_sprites(scene, sprites, count, false, s->name);
        if( scene_id < 0 )
        {
            for( int j = 0; j < count; j++ )
                if( sprites[j] )
                    toridraw_sprite_free(sprites[j]);
            free(sprites);
            s->state = DAT1_STATE_FAILED;
            return;
        }
        /* ownership of sprites[] transferred to UIScene */

        s->scene_id = scene_id;

        struct Dat1_SpriteMapEntry* entry = dashmap_search(sprite_map, s->name, DASHMAP_INSERT);
        if( entry )
        {
            strncpy(entry->name, s->name, sizeof(entry->name) - 1);
            entry->name[sizeof(entry->name) - 1] = '\0';
            entry->scene_id = scene_id;
            entry->atlas_index = s->atlas_index;
        }

        printf("dat1_sprite: loaded '%s' scene_id=%d\n", s->name, scene_id);
        s->state = DAT1_STATE_DONE;
        return;
    }

    case DAT1_STATE_DONE:
    case DAT1_STATE_FAILED:
        return;

    default:
        return;
    }

    /* Re-enter after a state change within the same tick (NEED_ARCHIVE -> CONVERT). */
    dat1_sprite_step(s, buildcache, scene, sprite_map);
}
