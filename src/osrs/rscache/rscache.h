#ifndef RSCACHE_H
#define RSCACHE_H

/*
 * RSCache public API — include this single header to use the library.
 * Compile unity.c once to link the implementation.
 */

/* entry points */
#include "disk/disk.h"
#include "dat2disk/dat2disk.h"
#include "dat1disk/dat1disk.h"

/* dat2a decoders */
#include "dat2a/dat2a_model.h"
#include "dat2a/dat2a_maps.h"
#include "dat2a/dat2a_frame.h"
#include "dat2a/dat2a_framemap.h"
#include "dat2a/dat2a_sprites.h"
#include "dat2a/dat2a_textures.h"
#include "dat2a/dat2a_component.h"
#include "dat2a/dat2a_configs.h"
#include "dat2a/dat2a_config_locs.h"
#include "dat2a/dat2a_config_floortype.h"
#include "dat2a/dat2a_config_object.h"
#include "dat2a/dat2a_config_npctype.h"
#include "dat2a/dat2a_config_idk.h"
#include "dat2a/dat2a_config_sequence.h"

/* dat1a decoders */
#include "dat1a/dat1a_anim_frame.h"
#include "dat1a/dat1a_config_obj.h"
#include "dat1a/dat1a_config_npc.h"
#include "dat1a/dat1a_config_idk.h"
#include "dat1a/dat1a_config_varp.h"
#include "dat1a/dat1a_config_spotanim.h"
#include "dat1a/dat1a_config_component.h"
#include "dat1a/dat1a_config_textures.h"
#include "dat1a/dat1a_pix8.h"
#include "dat1a/dat1a_pix32.h"
#include "dat1a/dat1a_pix_font.h"
#include "dat1a/dat1a_version_list_mapsquare.h"

#endif /* RSCACHE_H */
