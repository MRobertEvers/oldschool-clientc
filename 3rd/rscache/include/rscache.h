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
#include "disk.h"
#include "filelist.h"
#include "datatypes/model.h"
#include "datatypes/dat2_component.h"
// clang-format on

#endif
