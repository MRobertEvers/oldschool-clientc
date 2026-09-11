/*
 * RuneScript access to durable POH storage.
 *
 * No costs, requirements, room compatibility, hotspot membership, XP, or
 * instance geometry live here. Those are Construction rules and cache data.
 * This file only moves validated integers between the VM and ToriRSServerPohState.
 */

#include "torirs_server.h"

#include "torirs_server_save.h"
#include "ss_opcode.h"
#include "ssvm.h"

int
ToriRSServer_OpsPoh(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct ToriRSServer* srv = (struct ToriRSServer*)state->env->host.user;
    struct ToriRSServerPlayer* player =
        (struct ToriRSServerPlayer*)SSVM_Active(state, SSVM_ENT_PLAYER);

    (void)dot;
    if( !player )
        player = srv->active_player;
    switch( opcode )
    {
    case SS_OP_POH_STATE_RESET:
        ToriRSServer_PohReset(&player->poh);
        return 1;

    case SS_OP_POH_STATE_GET:
    {
        int32_t field;

        if( !SSVM_PopInt(state, &field) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_PohGet(&player->poh, field));
        return 1;
    }

    case SS_OP_POH_STATE_SET:
    {
        int32_t field;
        int32_t value;

        if( !SSVM_PopInt(state, &value) || !SSVM_PopInt(state, &field) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_PohSet(&player->poh, field, value));
        return 1;
    }

    case SS_OP_POH_ROOM_ADD:
    {
        int32_t values[6];

        for( int i = 5; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        SSVM_PushInt(state,
                     ToriRSServer_PohRoomAdd(&player->poh, values[0], values[1],
                                          values[2], values[3], values[4],
                                          values[5]));
        return 1;
    }

    case SS_OP_POH_ROOM_COUNT:
        SSVM_PushInt(state, player->poh.room_count);
        return 1;

    case SS_OP_POH_ROOM_GET:
    {
        int32_t room;
        int32_t field;

        if( !SSVM_PopInt(state, &field) || !SSVM_PopInt(state, &room) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_PohRoomGet(&player->poh, room, field));
        return 1;
    }

    case SS_OP_POH_DECOR_SET:
    {
        int32_t values[5];

        for( int i = 4; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        SSVM_PushInt(
            state,
            ToriRSServer_PohDecorationSet(&player->poh, values[0], values[1],
                                       values[2], values[3], values[4]));
        return 1;
    }

    case SS_OP_POH_DECOR_GET:
    {
        int32_t room;
        int32_t hotspot;
        int32_t field;

        if( !SSVM_PopInt(state, &field) || !SSVM_PopInt(state, &hotspot) ||
            !SSVM_PopInt(state, &room) )
            return 1;
        SSVM_PushInt(
            state,
            ToriRSServer_PohDecorationGet(&player->poh, room, hotspot, field));
        return 1;
    }

    case SS_OP_POH_STATE_COMMIT:
        SSVM_PushInt(
            state,
            ToriRSServer_PohValidate(&player->poh) &&
                ToriRSServer_SavePlayer(player, ToriRSServer_SavePath(player->display_name)));
        return 1;

    case SS_OP_POH_ROOM_SET:
    {
        int32_t room;
        int32_t field;
        int32_t value;

        if( !SSVM_PopInt(state, &value) || !SSVM_PopInt(state, &field) ||
            !SSVM_PopInt(state, &room) )
            return 1;
        SSVM_PushInt(
            state, ToriRSServer_PohRoomSet(&player->poh, room, field, value));
        return 1;
    }

    case SS_OP_POH_ROOM_REMOVE:
    {
        int32_t room;

        if( !SSVM_PopInt(state, &room) )
            return 1;
        SSVM_PushInt(state, ToriRSServer_PohRoomRemove(&player->poh, room));
        return 1;
    }

    default:
        return 0;
    }
}
