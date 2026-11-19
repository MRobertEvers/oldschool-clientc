#include "net/net_once.h"

#include <assert.h>
#include <string.h>

void
NetOnce_Reset(struct NetOnce* once)
{
    assert(once);
    memset(once, 0, sizeof(*once));
}

bool
NetOnce_TakeFirstPacket(struct NetOnce* once)
{
    assert(once);
    if( once->first_packet_marked )
        return false;
    once->first_packet_marked = 1;
    return true;
}

bool
NetOnce_CheatFires(
    struct NetOnce* once,
    uint64_t logic_cycle,
    int every)
{
    bool fire = false;

    assert(once);
    if( !once->cheat_sent )
        fire = true;
    else if( every > 0 && (logic_cycle % every) == 0 )
        fire = true;
    if( fire )
        once->cheat_sent = 1;
    return fire;
}
