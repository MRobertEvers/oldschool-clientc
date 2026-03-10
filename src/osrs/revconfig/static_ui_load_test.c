#include "osrs/buildcachedat.h"
#include "revconfig.h"
#include "revconfig_load.h"
#include "static_ui_load.h"
#include "uiscene.h"

#include <stdio.h>

int
main()
{
    const char* filename = "/Users/matthewevers/Documents/git_repos/3draster/src/osrs/revconfig/"
                           "configs/rev_254_2/rev_245_2_cache.ini";

    struct RevConfigBuffer* buffer = revconfig_buffer_new(16);
    uint32_t field_count = 0;

    revconfig_load_fields_from_ini(filename, buffer);

    struct BuildCacheDat* buildcachedat = buildcachedat_new();
    struct CacheDat* cache_dat =
        cache_dat_new_from_directory("/Users/matthewevers/Documents/git_repos/3draster/cache254");

    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_MEDIA_2D_GRAPHICS);

    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);
    cache_dat_archive_free(archive);

    buildcachedat_set_2d_media_jagfile(buildcachedat, filelist);

    struct UIScene* static_ui = uiscene_new(32);
    static_ui_from_revconfig_buildcachedat(static_ui, buildcachedat, buffer);
    return 0;
}