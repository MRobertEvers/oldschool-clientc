/*
 * RuneScript access to durable POH storage.
 *
 * No costs, requirements, room compatibility, hotspot membership, XP, or
 * instance geometry live here. Those are Construction rules and cache data.
 * This file only moves validated integers between the VM and Mock230PohState.
 */

#include "mock230.h"

#include "mock230_save.h"
#include "ss_opcode.h"
#include "ssvm.h"

int
mock230_ops_poh(
    struct SSVM_State* state,
    int opcode,
    int dot)
{
    struct Mock230Server* srv = (struct Mock230Server*)state->env->host.user;
    struct Mock230Player* player = srv->active_player;

    (void)dot;
    switch( opcode )
    {
    case SS_OP_POH_STATE_RESET:
        mock230_poh_reset(&player->poh);
        return 1;

    case SS_OP_POH_STATE_GET:
    {
        int32_t field;

        if( !SSVM_PopInt(state, &field) )
            return 1;
        SSVM_PushInt(state, mock230_poh_get(&player->poh, field));
        return 1;
    }

    case SS_OP_POH_STATE_SET:
    {
        int32_t field;
        int32_t value;

        if( !SSVM_PopInt(state, &value) || !SSVM_PopInt(state, &field) )
            return 1;
        SSVM_PushInt(state, mock230_poh_set(&player->poh, field, value));
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
                     mock230_poh_room_add(&player->poh, values[0], values[1],
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
        SSVM_PushInt(state, mock230_poh_room_get(&player->poh, room, field));
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
            mock230_poh_decoration_set(&player->poh, values[0], values[1],
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
            mock230_poh_decoration_get(&player->poh, room, hotspot, field));
        return 1;
    }

    case SS_OP_POH_STATE_COMMIT:
        SSVM_PushInt(
            state,
            mock230_poh_validate(&player->poh) &&
                mock230_save_player(player, mock230_save_path(player->display_name)));
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
            state, mock230_poh_room_set(&player->poh, room, field, value));
        return 1;
    }

    case SS_OP_POH_ROOM_REMOVE:
    {
        int32_t room;

        if( !SSVM_PopInt(state, &room) )
            return 1;
        SSVM_PushInt(state, mock230_poh_room_remove(&player->poh, room));
        return 1;
    }

    default:
        return 0;
    }
}
