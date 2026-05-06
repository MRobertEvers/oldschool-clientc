#include "gamenet_send.h"

#include "osrs/core/revision.h"
#include "osrs/revs/lc245_2/gamenet_rev245_2_send.h"

void
gamenet_send_move_gameclick(
    struct GGame* g,
    int           run,
    const int*    path_x,
    const int*    path_z,
    int           path_len)
{
    switch( revision_active()->kind )
    {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_move_gameclick(g, run, path_x, path_z, path_len);
        break;
    default:
        break;
    }
}

void
gamenet_send_move_opclick(
    struct GGame* g,
    int           run,
    const int*    path_x,
    const int*    path_z,
    int           path_len)
{
    switch( revision_active()->kind )
    {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_move_opclick(g, run, path_x, path_z, path_len);
        break;
    default:
        break;
    }
}
