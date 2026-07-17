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
// clang-format on

#endif
