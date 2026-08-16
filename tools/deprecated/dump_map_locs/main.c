/*
 * Dump map scenery loc placements (decrypted) for a centerzone region.
 *
 * Usage:
 *   dump_map_locs <cache_dir> <center_x> <center_z> <scene_size> [shape_filter]
 *
 * Prints TSV: mapx mapz loc_id shape orientation local_x local_z level
 */

#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_xtea_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
chunk_range_from_centerzone(
    int center_x,
    int center_z,
    int scene_size,
    int* sw_x,
    int* sw_z,
    int* ne_x,
    int* ne_z)
{
    int zone_padding = scene_size / (2 * 8);
    int zone_sw_x = center_x - zone_padding;
    int zone_sw_z = center_z - zone_padding;
    int zone_ne_x = center_x + zone_padding;
    int zone_ne_z = center_z + zone_padding;

    *sw_x = zone_sw_x / 8;
    *sw_z = zone_sw_z / 8;
    *ne_x = zone_ne_x / 8;
    *ne_z = zone_ne_z / 8;
}

int
main(int argc, char** argv)
{
    if( argc < 5 || argc > 6 )
    {
        fprintf(
            stderr,
            "Usage: %s <cache_dir> <center_x> <center_z> <scene_size> [shape_filter]\n",
            argv[0]);
        return 1;
    }

    const char* cache_dir = argv[1];
    int center_x = atoi(argv[2]);
    int center_z = atoi(argv[3]);
    int scene_size = atoi(argv[4]);
    int shape_filter = (argc == 6) ? atoi(argv[5]) : -1;

    /* Missing keys are not an error: map archives are unencrypted from
     * OldSchool 237 on, so cache.osrs239 and friends ship no xteas.json at
     * all. Refusing to run there made this tool unusable on every modern
     * cache. A keyed cache still needs the file, and a loc archive that
     * really is encrypted simply decodes to nothing — visible as an empty
     * dump rather than as a wrong one. */
    char xtea_path[1024];
    snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", cache_dir);
    if( RSCacheShared_XteaConfigLoadKeys(xtea_path) < 0 )
        fprintf(stderr, "note: no %s (unencrypted maps expected for rev >= 237)\n", xtea_path);

    struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "Failed to open cache: %s\n", cache_dir);
        return 1;
    }

    int sw_x, sw_z, ne_x, ne_z;
    chunk_range_from_centerzone(center_x, center_z, scene_size, &sw_x, &sw_z, &ne_x, &ne_z);

    printf("# center=(%d,%d) scene_size=%d chunks x=[%d,%d] z=[%d,%d]\n",
           center_x,
           center_z,
           scene_size,
           sw_x,
           ne_x,
           sw_z,
           ne_z);
    printf("mapx\tmapz\tloc_id\tshape\torientation\tlocal_x\tlocal_z\tlevel\n");

    for( int mapx = sw_x; mapx <= ne_x; mapx++ )
    {
        for( int mapz = sw_z; mapz <= ne_z; mapz++ )
        {
            struct RSCacheDat2A_MapLocs* map_locs =
                RSCacheDat2A_MapLocsNewFromCache(cache, mapx, mapz);
            if( !map_locs )
                continue;

            for( int i = 0; i < map_locs->locs_count; i++ )
            {
                struct RSCacheDat2A_MapLoc* loc = &map_locs->locs[i];
                if( shape_filter >= 0 && loc->shape_select != shape_filter )
                    continue;
                printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                       mapx,
                       mapz,
                       loc->loc_id,
                       loc->shape_select,
                       loc->orientation,
                       loc->chunk_pos_x,
                       loc->chunk_pos_z,
                       loc->chunk_pos_level);
            }

            RSCacheDat2A_MapLocsFree(map_locs);
        }
    }

    RSCacheDat2Disk_Free(cache);
    return 0;
}
