#ifndef GAMECACHE2_INTERNAL_H
#define GAMECACHE2_INTERNAL_H

#include "dat1/gamecache2_dat1.h"
#include "dat2/gamecache2_dat2.h"
#include "graphics/dash.h"

enum GameCache2Mode
{
    GAMECACHE2_DAT1 = 0,
    GAMECACHE2_DAT2 = 1,
};

struct GameCache2
{
    enum GameCache2Mode mode;
    union
    {
        struct GameCache2Dat1* dat1;
        struct GameCache2Dat2* dat2;
    } u;
};

#endif