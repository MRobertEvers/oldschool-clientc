#ifndef RSCACHE_U_C
#define RSCACHE_U_C

/*
 * RSCache unity build — dependency order:
 *   shared (infra) -> disk -> dat2disk -> dat1a -> dat1disk -> filelist -> dat2a
 */

// clang-format off

/* shared */
#include "shared/shared_string_utils.c"
#include "shared/shared_archive.c"
#include "shared/shared_compression.c"
#include "shared/shared_xtea.c"
#include "shared/shared_rs_buffer.c"
#include "shared/shared_bit_buffer.c"
#include "shared/shared_archive_decompress.c"
#include "shared/shared_xtea_config.c"

/* disk */
#include "disk/disk.c"

/* dat2disk */
#include "dat2disk/dat2disk_reference_table.c"
#include "dat2disk/dat2disk.c"
#include "dat2disk/dat2disk_inet.c"
#include "dat2disk/dat2disk_inet_indexeddb.c"

/* dat1a (decoders needed by dat1disk init) */
#include "dat1a/dat1a_version_list_mapsquare.c"
#include "dat1a/dat1a_anim_frame.c"
#include "dat1a/dat1a_config_obj.c"
#include "dat1a/dat1a_config_npc.c"
#include "dat1a/dat1a_config_idk.c"
#include "dat1a/dat1a_config_varp.c"
#include "dat1a/dat1a_config_spotanim.c"
#include "dat1a/dat1a_config_component.c"
#include "dat1a/dat1a_config_textures.c"
#include "dat1a/dat1a_pix8.c"
#include "dat1a/dat1a_pix32.c"
#include "dat1a/dat1a_pix_font.c"

/* dat1disk */
#include "dat1disk/dat1disk.c"

/* shared filelist (depends on dat2disk + dat1disk headers) */
#include "shared/shared_file_list.c"

/* dat2a */
#include "dat2a/dat2a_noise.c"
#include "dat2a/dat2a_model.c"
#include "dat2a/dat2a_maps.c"
#include "dat2a/dat2a_frame.c"
#include "dat2a/dat2a_framemap.c"
#include "dat2a/dat2a_sprites.c"
#include "dat2a/dat2a_textures.c"
#include "dat2a/dat2a_component.c"
#include "dat2a/dat2a_config_locs.c"
#include "dat2a/dat2a_config_floortype.c"
#include "dat2a/dat2a_config_object.c"
#include "dat2a/dat2a_config_npctype.c"
#include "dat2a/dat2a_config_idk.c"
#include "dat2a/dat2a_config_sequence.c"
#include "dat2a/dat2a_animaya.c"
#include "dat2a/dat2a_skeletalbase.c"

// clang-format on

#endif /* RSCACHE_U_C */
