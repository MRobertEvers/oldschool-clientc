#include "gamenet_parse.h"

#include "osrs/core/packetbuffer.h"
#include "osrs/core/revision.h"
#include "osrs/game.h"

void
gamenet_parse(struct GGame* game)
{
    if( !game || !game->packet_buffer )
        return;

    if( packetbuffer_ready(game->packet_buffer) )
    {
        revision_serverprot_parse(
            &game->revision,
            game,
            packetbuffer_packet_type(game->packet_buffer),
            packetbuffer_data(game->packet_buffer),
            packetbuffer_size(game->packet_buffer));

        packetbuffer_reset(game->packet_buffer);
    }
}
