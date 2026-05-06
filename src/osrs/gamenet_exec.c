#include "gamenet_exec.h"

#include "osrs/core/revision.h"
#include "osrs/game.h"
#include "osrs/revs/lc245_2/gamenet_rev245_2_exec.h"
#include "osrs/revs/lc245_2/gameproto_rev245_2_packets.h"

#include <assert.h>

void
gamenet_exec(struct GGame* game, struct RevPacket_LC245_2* packet)
{
    if( !game || !packet )
        return;
    switch( game->revision.kind )
    {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_exec_dispatch_v1(game, packet);
        break;
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "gamenet_exec: revision not wired");
        break;
    }
}
