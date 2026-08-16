#ifndef RSCACHE_H
#define RSCACHE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * RSCache public API — include this single header to use the library.
 * Compile rscache_unity.c once to link the implementation.
 */

/* `enum RSCache_Game` and `struct RSCache` now live in rscache_profile.h, which
 * is included below. The struct used to be declared here with two fields and no
 * users; it is now the cache-identity value every revision-sensitive codec takes.
 * See rscache_profile.h for what it carries and why. */

// Unity
// clang-format off
#include "rscache_profile.h"
#include "rsbuffer.h"
#include "checksum.h"
#include "compression.h"
#include "archive.h"
#include "reference_table.h"
#include "xtea_config.h"
#include "dat2disk.h"
#include "cache_edit.h"
#include "dat1disk.h"
#include "filelist.h"
#include "datatypes/model.h"
#include "datatypes/dat2_component.h"
#include "datatypes/mapsquares.h"
#include "datatypes/noise.h"
#include "datatypes/maps.h"
#include "datatypes/cs2_script.h"
#include "datatypes/cs2_opcode_decode.h"
#include "datatypes/clientscript.h"
#include "datatypes/dat2_configs.h"
#include "datatypes/dat1_config_obj.h"
#include "datatypes/dat1_config_idk.h"
#include "datatypes/dat1_config_npc.h"
#include "datatypes/dat1_config_spotanim.h"
#include "datatypes/dat2_config_spotanim.h"
#include "datatypes/dat2_config_obj.h"
#include "datatypes/dat2_config_idk.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_entity_ops.h"
#include "datatypes/dat2_config_bas.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_flo.h"
#include "datatypes/dat2_texture.h"
#include "datatypes/sound_synth.h"
#include "datatypes/sound_render.h"
#include "datatypes/sound_vorbis.h"
#include "datatypes/music_patch.h"
#include "datatypes/music_song.h"
#include "datatypes/dat2_sprites.h"
#include "datatypes/dat2_config_enum.h"
#include "datatypes/dat2_config_struct.h"
#include "datatypes/dat2_config_db.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_config_var.h"
#include "datatypes/dat2_config_inv.h"
#include "datatypes/dat2_config_healthbar.h"
#include "datatypes/dat2_config_hitsplat.h"
#include "datatypes/dat2_config_mapelement.h"
#include "datatypes/dat2_worldmap.h"
#include "datatypes/dat2_worldmap_geography.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_config_soundscape.h"
#include "datatypes/dat2_animaya.h"
#include "datatypes/dat2_skeletalbase.h"
#include "datatypes/dat1_anim_frame.h"
#include "datatypes/dat1_config_seq.h"
#include "datatypes/dat1_pix8.h"
#include "datatypes/dat1_pix32.h"
#include "datatypes/dat1_pix_font.h"
#include "datatypes/dat2_font_metrics.h"
#include "datatypes/dat1_config_component.h"
#include "datatypes/dat1_version_list.h"
/* Above the datatypes: a revision module names the codecs it binds. */
#include "revisions/revisions.h"
// clang-format on

#endif
