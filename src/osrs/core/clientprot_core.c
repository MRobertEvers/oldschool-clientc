#include "clientprot_core.h"

#include "osrs/core/revision.h"
#include "osrs/game.h"
#include "osrs/rscache/rsbuf.h"
#include "tori_rs.h"

#include <string.h>

/* Maximum outbound packet size (generous upper bound). */
#define CLIENTPROT_SCRATCH_SIZE 2048

void
clientprot_core_emit(
    struct GGame* game,
    enum ClientProtOpKind kind,
    void* args)
{
    if( !game )
        return;

    /* Only send NO_TIMEOUT and MAP_BUILD_COMPLETE to the server while the
     * rest of UI interactions are being stubbed out locally.  All other packet
     * types go through local-state updates (varp optimistic, try_move, etc.)
     * but must not reach the wire yet. */
    if( kind != CLIENTPROT_OP_NO_TIMEOUT && kind != CLIENTPROT_OP_MAP_BUILD_COMPLETE )
        return;

    /* Stack-allocated scratch buffer; avoids any heap for normal packets. */
    uint8_t scratch[CLIENTPROT_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, (int)sizeof(scratch));

    const struct Revision* rev = revision_active();
    int rc = revision_clientprot_emit(rev, &b, game, (int)kind, args);
    if( rc != 0 || b.position == 0 )
        return;

    LibToriRS_NetSend(game, scratch, (int)b.position);
}
