#include "pkt_player_appearance.h"

#include "net/jbase37.h"

#include <rsbuffer.h>
#include <string.h>

static int
anim_g2(struct RSCache_Buffer* rsbuf)
{
    int value = g2(rsbuf);
    return value == 65535 ? -1 : value;
}

int
PktPlayerAppearance_Decode(
    struct PktPlayerAppearance* out,
    uint8_t const* buf,
    int len)
{
    struct RSCache_Buffer rsbuf;

    /* Minimum: 2 + 12 + 5 + 14 + 8 + 1 (all slots empty). */
    if( !out || !buf || len < 42 )
        return 0;

    memset(out, 0, sizeof(*out));
    RSCache_BufferInit(&rsbuf, (uint8_t*)buf, (uint32_t)len);

    out->gender = g1(&rsbuf);
    out->headicon = g1(&rsbuf);

    for( int i = 0; i < 12; i++ )
    {
        int msb = g1(&rsbuf);
        out->slots[i] = msb == 0 ? 0 : ((msb << 8) | g1(&rsbuf));
    }

    for( int i = 0; i < 5; i++ )
        out->colors[i] = g1(&rsbuf);

    out->readyanim = anim_g2(&rsbuf);
    out->turnanim = anim_g2(&rsbuf);
    out->walkanim = anim_g2(&rsbuf);
    out->walkanim_b = anim_g2(&rsbuf);
    out->walkanim_l = anim_g2(&rsbuf);
    out->walkanim_r = anim_g2(&rsbuf);
    out->runanim = anim_g2(&rsbuf);

    {
        uint64_t name37 = (uint64_t)g8(&rsbuf);
        base37tostr(name37, out->name, sizeof(out->name));
    }
    out->combat_level = g1(&rsbuf);

    return (int)rsbuf.position <= len;
}
