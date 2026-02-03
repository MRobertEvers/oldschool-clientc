#include "player_appearance.h"

#include "jbase37.h"
#include "osrs/rscache/rsbuf.h"

void
player_appearance_decode(
    struct PlayerAppearance* appearance,
    uint8_t* buf,
    int len)
{
    //
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, buf, len);
    int gender = g1(&rsbuf);
    int head_icon = g1(&rsbuf);

    for( int i = 0; i < 12; i++ )
    {
        int msb = g1(&rsbuf);
        if( msb == 0 )
        {
            appearance->appearance[i] = 0;
        }
        else
        {
            appearance->appearance[i] = (msb << 8) | g1(&rsbuf);
        }
    }

    for( int i = 0; i < 5; i++ )
    {
        appearance->color[i] = g1(&rsbuf);
    }

    int readyanim = g2(&rsbuf);
    if( readyanim == 65535 )
        readyanim = -1;
    appearance->readyanim = readyanim;

    int turnanim = g2(&rsbuf);
    if( turnanim == 65535 )
        turnanim = -1;
    appearance->turnanim = turnanim;
    int walkanim = g2(&rsbuf);
    if( walkanim == 65535 )
        walkanim = -1;
    appearance->walkanim = walkanim;
    int walkanim_b = g2(&rsbuf);
    if( walkanim_b == 65535 )
        walkanim_b = -1;
    appearance->walkanim_b = walkanim_b;
    int walkanim_l = g2(&rsbuf);
    if( walkanim_l == 65535 )
        walkanim_l = -1;
    appearance->walkanim_l = walkanim_l;
    int walkanim_r = g2(&rsbuf);
    if( walkanim_r == 65535 )
        walkanim_r = -1;
    appearance->walkanim_r = walkanim_r;
    int runanim = g2(&rsbuf);
    if( runanim == 65535 )
        runanim = -1;
    appearance->runanim = runanim;
    uint64_t name = g8(&rsbuf);
    base37tostr(name, appearance->name, sizeof(appearance->name));

    int combat_level = g1(&rsbuf);
    appearance->combat_level = combat_level;
}