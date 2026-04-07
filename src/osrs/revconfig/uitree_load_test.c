#include "bmp.h"
#include "osrs/buildcachedat.h"
#include "revconfig.h"
#include "revconfig_load.h"
#include "uiscene.h"
#include "uitree.h"
#include "uitree_load.h"

#include <stdio.h>
#include <stdlib.h>

int
main()
{
    const char* filename_cache = "../src/osrs/revconfig/"
                                 "configs/rev_245_2/rev_245_2_cache.ini";
    char const* filename_ui = "../src/osrs/revconfig/"
                              "configs/rev_245_2/rev_245_2_ui.ini";

    struct RevConfigBuffer* buffer = revconfig_buffer_new(16);
    uint32_t field_count = 0;

    revconfig_load_fields_from_ini(filename_cache, buffer);
    revconfig_load_fields_from_ini(filename_ui, buffer);

    struct BuildCacheDat* buildcachedat = buildcachedat_new();
    struct CacheDat* cache_dat = cache_dat_new_from_directory("../cache254");

    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_MEDIA_2D_GRAPHICS);

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);
    cache_dat_archive_free(archive);

    buildcachedat_set_2d_media_jagfile(buildcachedat, filelist);

    struct UIScene* ui_scene = uiscene_new(128);
    struct UITree* ui = uitree_new(16);
    struct UIInventoryPool* inv_pool = uitree_inv_pool_new(8);
    uitree_from_revconfig_buildcachedat(
        ui, ui_scene, NULL, buildcachedat, inv_pool, NULL, buffer);
    uitree_inv_pool_free(inv_pool);

    struct DashGraphics* dash = dash_new();
    struct DashViewPort view_port = { 0 };
    view_port.width = 1024;
    view_port.height = 1024;
    view_port.stride = 1024;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = 1024;
    view_port.clip_bottom = 1024;

    int* pixels = malloc(1024 * 1024 * sizeof(int));
    for( int i = 0; i < ui->component_count; i++ )
    {
        struct StaticUIComponent* component = &ui->components[i];
        if( component->type == UIELEM_BUILTIN_SPRITE )
        {
            assert(
                component->u.sprite.scene_id != -1 &&
                "Sprite components must have a valid scene_id");
            struct UISceneElement* element =
                uiscene_element_at(ui_scene, component->u.sprite.scene_id);
            printf(
                "Blitting component %d of type %d using scene element id %d at x=%d, y=%d\n",
                i,
                component->type,
                component->u.sprite.scene_id,
                component->position.x,
                component->position.y);
            if( element )
            {
                struct DashSprite* sprite = element->dash_sprites[component->u.sprite.atlas_index];
                dash2d_blit_sprite(
                    dash,
                    sprite,
                    &view_port,
                    component->position.x + sprite->crop_x,
                    component->position.y + sprite->crop_y,
                    pixels);
            }
            else
            {
                printf(
                    "Failed to find sprite with id %d for sprite component\n",
                    component->u.sprite.scene_id);
            }
        }
    }

    bmp_write_file("output.bmp", pixels, 1024, 1024);

    free(pixels);

    return 0;
}