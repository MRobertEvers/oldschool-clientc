#include "game/rs_hitsplat.h"

#include <stdlib.h>
#include <string.h>

void
RS_Hitsplats_Init(struct RS_Hitsplats* hitsplats)
{
    memset(hitsplats, 0, sizeof(*hitsplats));
}

void
RS_Hitsplats_Free(struct RS_Hitsplats* hitsplats)
{
    free(hitsplats->sprite_ids);
    hitsplats->sprite_ids = NULL;
    hitsplats->count = 0;
}

int
RS_Hitsplats_SetTypes(
    struct RS_Hitsplats* hitsplats,
    int* sprite_ids,
    int count)
{
    if( !sprite_ids || count <= 0 )
        return 0;
    free(hitsplats->sprite_ids);
    hitsplats->sprite_ids = sprite_ids;
    hitsplats->count = count;
    return 1;
}

int
RS_Hitsplats_SpriteFor(
    struct RS_Hitsplats const* hitsplats,
    int hitsplat_type)
{
    if( !hitsplats->sprite_ids || hitsplat_type < 0 || hitsplat_type >= hitsplats->count )
        return -1;
    return hitsplats->sprite_ids[hitsplat_type];
}
