#ifndef RSCACHE_H
#define RSCACHE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * RSCache public API — include this single header to use the library.
 * Compile rscache_unity.c once to link the implementation.
 */

enum RSCache_Game
{
    RSCACHE_GAME_OLDSCHOOL = 0,
    RSCACHE_GAME_RS2 = 1,
};

struct RSCache
{
    int game;
    int version;
};

// Unity
// clang-format off
#include "rsbuffer.h"
#include "archive.h"
#include "reference_table.h"
#include "xtea_config.h"
#include "dat2disk.h"
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
#include "datatypes/dat2_config_obj.h"
#include "datatypes/dat2_config_idk.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/dat2_config_loc.h"
#include "datatypes/dat2_config_flo.h"
#include "datatypes/dat2_texture.h"
#include "datatypes/dat2_sprites.h"
#include "datatypes/dat2_config_enum.h"
#include "datatypes/dat2_config_struct.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/dat2_frame.h"
#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_animaya.h"
#include "datatypes/dat1_anim_frame.h"
#include "datatypes/dat1_pix8.h"
#include "datatypes/dat1_pix32.h"
#include "datatypes/dat1_pix_font.h"
#include "datatypes/dat1_config_component.h"
// clang-format on

#endif
