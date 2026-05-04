#include "gameproto_process.h"

#include "osrs/core/revision.h"
#include "osrs/game.h"
#include "osrs/script_queue.h"

void
gameproto_process(struct GGame* game)
{
    if( !revision_has_pending(&game->revision, game) )
        return;
    struct ScriptArgs args = { .tag = SCRIPT_PKT_DISPATCH };
    script_queue_push(&game->script_queue, &args);
}
