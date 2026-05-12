#include "gamecache2.h"

#include "dat1/gamecache2_dat1.h"
#include "gamecache2_internal.h"

#include <assert.h>
#include <stdlib.h>

struct GameCache2*
GameCache2_New()
{
    struct GameCache2* gc2 = malloc(sizeof(struct GameCache2));
    if( !gc2 )
        return NULL;
    gc2->u.dat1 = GameCache2Dat1_New();
    return gc2;
}
