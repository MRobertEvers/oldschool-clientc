#include "clientprot_core.h"

#include "osrs/core/revision.h"
#include "osrs/game.h"
#include "osrs/isaac.h"
#include "osrs/rscache/rsbuf.h"
#include "tori_rs.h"

#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum outbound packet size (generous upper bound). */
#define CLIENTPROT_SCRATCH_SIZE 2048

/** Same masking as Client-TS `Packet.pIsaac`: wire[0] == (logical_opcode + isaac_next()) & 0xff.
 * `logical_op` is decoded from the wire byte using ISAAC — compare to `packetout.h`
 * (`revs/lc245_2/packetout_lc245_2.h`) PKTOUT_LC245_2_* (ClientGameProt `id` values). */
static char const*
clientprot_kind_name(enum ClientProtOpKind kind)
{
    switch( kind )
    {
    case CLIENTPROT_OP_NO_TIMEOUT:
        return "NO_TIMEOUT";
    case CLIENTPROT_OP_IDLE_TIMER:
        return "IDLE_TIMER";
    case CLIENTPROT_OP_EVENT_TRACKING:
        return "EVENT_TRACKING";
    case CLIENTPROT_OP_MOVE_GAMECLICK:
        return "MOVE_GAMECLICK";
    case CLIENTPROT_OP_MOVE_MINIMAPCLICK:
        return "MOVE_MINIMAPCLICK";
    case CLIENTPROT_OP_MOVE_OPCLICK:
        return "MOVE_OPCLICK";
    case CLIENTPROT_OP_CHAT_SETMODE:
        return "CHAT_SETMODE";
    case CLIENTPROT_OP_MESSAGE_PUBLIC:
        return "MESSAGE_PUBLIC";
    case CLIENTPROT_OP_MESSAGE_PRIVATE:
        return "MESSAGE_PRIVATE";
    case CLIENTPROT_OP_CLIENT_CHEAT:
        return "CLIENT_CHEAT";
    case CLIENTPROT_OP_RESUME_P_COUNTDIALOG:
        return "RESUME_P_COUNTDIALOG";
    case CLIENTPROT_OP_RESUME_PAUSEBUTTON:
        return "RESUME_PAUSEBUTTON";
    case CLIENTPROT_OP_INV_BUTTON:
        return "INV_BUTTON";
    case CLIENTPROT_OP_INV_BUTTOND:
        return "INV_BUTTOND";
    case CLIENTPROT_OP_IF_BUTTON:
        return "IF_BUTTON";
    case CLIENTPROT_OP_CLOSE_MODAL:
        return "CLOSE_MODAL";
    case CLIENTPROT_OP_OPHELD:
        return "OPHELD";
    case CLIENTPROT_OP_OPHELDT:
        return "OPHELDT";
    case CLIENTPROT_OP_OPHELDU:
        return "OPHELDU";
    case CLIENTPROT_OP_OPLOC:
        return "OPLOC";
    case CLIENTPROT_OP_OPLOCT:
        return "OPLOCT";
    case CLIENTPROT_OP_OPLOCU:
        return "OPLOCU";
    case CLIENTPROT_OP_OPNPC:
        return "OPNPC";
    case CLIENTPROT_OP_OPNPCT:
        return "OPNPCT";
    case CLIENTPROT_OP_OPNPCU:
        return "OPNPCU";
    case CLIENTPROT_OP_OPOBJ:
        return "OPOBJ";
    case CLIENTPROT_OP_OPOBJT:
        return "OPOBJT";
    case CLIENTPROT_OP_OPOBJU:
        return "OPOBJU";
    case CLIENTPROT_OP_OPPLAYER:
        return "OPPLAYER";
    case CLIENTPROT_OP_OPPLAYERT:
        return "OPPLAYERT";
    case CLIENTPROT_OP_OPPLAYERU:
        return "OPPLAYERU";
    case CLIENTPROT_OP_FRIEND_ADD:
        return "FRIEND_ADD";
    case CLIENTPROT_OP_FRIEND_DEL:
        return "FRIEND_DEL";
    case CLIENTPROT_OP_IGNORE_ADD:
        return "IGNORE_ADD";
    case CLIENTPROT_OP_IGNORE_DEL:
        return "IGNORE_DEL";
    case CLIENTPROT_OP_IF_PLAYERDESIGN:
        return "IF_PLAYERDESIGN";
    case CLIENTPROT_OP_TUTORIAL_CLICKSIDE:
        return "TUTORIAL_CLICKSIDE";
    case CLIENTPROT_OP_REPORT_ABUSE:
        return "REPORT_ABUSE";
    default:
        return "?";
    }
}

static int
clientprot_debug_pkout_enabled(void)
{
    static int cached = -1;
    if( cached >= 0 )
        return cached;
    char const* e = getenv("TORI_DEBUG_PKTOUT");
    /* Default on; set TORI_DEBUG_PKTOUT=0 to silence [pkout] spam. */
    if( e && e[0] == '0' && e[1] == '\0' )
        cached = 0;
    else
        cached = 1;
    return cached;
}

static void
clientprot_debug_pkout_print(
    struct GGame* game,
    enum ClientProtOpKind kind,
    uint8_t const* scratch,
    int len,
    int isaac_delta_byte)
{
    if( len <= 0 )
        return;

    int inferred_logical = (scratch[0] - isaac_delta_byte + 256) & 0xff;

    printf(
        "[pkout] kind=%s logical_op=%d wire0=0x%02x isaac_low_byte=0x%02x size=%d "
        "(logical_op vs packetout_lc245_2.h PKTOUT_LC245_2_*)\n",
        clientprot_kind_name(kind),
        inferred_logical,
        scratch[0],
        isaac_delta_byte & 0xff,
        len);

    if( len > 1 )
    {
        fputs("[pkout]   payload:", stdout);
        int max = len - 1;
        if( max > 48 )
            max = 48;
        for( int i = 1; i <= max; i++ )
            printf(" %02x", scratch[i]);
        if( len - 1 > max )
            fputs(" ...", stdout);
        fputc('\n', stdout);
    }

    (void)game;
}

void
clientprot_core_emit(
    struct GGame* game,
    enum ClientProtOpKind kind,
    void* args)
{
    if( !game )
        return;

    int dbg = clientprot_debug_pkout_enabled();
    int isaac_delta_byte = 0;
    struct Isaac* isaac_peek = NULL;

    if( dbg && game->random_out )
    {
        size_t z = isaac_state_size();
        void* snap = alloca(z);
        isaac_get_state(game->random_out, snap);
        isaac_peek = isaac_from_state(snap);
        if( isaac_peek )
            isaac_delta_byte = isaac_next(isaac_peek) & 0xff;
    }

    uint8_t scratch[CLIENTPROT_SCRATCH_SIZE];
    struct RSBuffer b;
    rsbuf_init(&b, (int8_t*)scratch, (int)sizeof(scratch));

    const struct Revision* rev = revision_active();
    int rc = revision_clientprot_emit(rev, &b, game, (int)kind, args);

    if( isaac_peek )
    {
        isaac_free(isaac_peek);
        isaac_peek = NULL;
    }

    if( rc != 0 || b.position == 0 )
        return;

    if( dbg )
        clientprot_debug_pkout_print(game, kind, scratch, (int)b.position, isaac_delta_byte);

    LibToriRS_NetSend(game, scratch, (int)b.position);
}
