/*
 * RuneScript access to durable POH storage.
 *
 * No costs, requirements, room compatibility, hotspot membership, XP, or
 * instance geometry live here. Those are Construction rules and cache data.
 * This file only moves validated integers between the VM and ToriRSServerPohState.
 */

#include "torirs_server.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "torirs_server_save.h"
#include "ss_opcode.h"
#include "ssvm.h"

/* Drop every advert whose lifetime has passed, keeping the rest in the order
 * they were added. Only COUNT and ADD prune, so the row indices a script read
 * from COUNT stay valid for the GET/NAME calls of the same script turn. */
static void
poh_board_prune(struct ToriRSServer* srv)
{
    int kept = 0;

    for( int i = 0; i < srv->poh_board_count; i++ )
    {
        if( srv->poh_board[i].expires_tick <= srv->tick )
            continue;
        if( kept != i )
            srv->poh_board[kept] = srv->poh_board[i];
        kept++;
    }
    srv->poh_board_count = kept;
}

static int
poh_board_find(
    const struct ToriRSServer* srv,
    const char* name)
{
    for( int i = 0; i < srv->poh_board_count; i++ )
    {
        if( strcmp(srv->poh_board[i].name, name) == 0 )
            return i;
    }
    return -1;
}

static int
poh_board_remove_at(
    struct ToriRSServer* srv,
    int row)
{
    if( row < 0 )
        return 0;
    memmove(&srv->poh_board[row], &srv->poh_board[row + 1],
            (size_t)(srv->poh_board_count - row - 1) * sizeof(srv->poh_board[0]));
    srv->poh_board_count--;
    return 1;
}

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

    case SS_OP_POH_BOARD_ADD:
    {
        int32_t values[TORIRSSERVER_POH_BOARD_FIELDS + 1];
        int row;

        for( int i = TORIRSSERVER_POH_BOARD_FIELDS; i >= 0; i-- )
        {
            if( !SSVM_PopInt(state, &values[i]) )
                return 1;
        }
        assert(values[TORIRSSERVER_POH_BOARD_FIELDS] > 0);
        poh_board_prune(srv);
        /* A re-advert replaces the row and moves it to the end, as if new. */
        poh_board_remove_at(srv, poh_board_find(srv, player->display_name));
        if( srv->poh_board_count >= TORIRSSERVER_POH_BOARD_MAX )
        {
            SSVM_PushInt(state, 0);
            return 1;
        }
        row = srv->poh_board_count++;
        snprintf(srv->poh_board[row].name, sizeof(srv->poh_board[row].name), "%s",
                 player->display_name);
        memcpy(srv->poh_board[row].fields, values, sizeof(srv->poh_board[row].fields));
        srv->poh_board[row].expires_tick = srv->tick + values[TORIRSSERVER_POH_BOARD_FIELDS];
        SSVM_PushInt(state, 1);
        return 1;
    }

    case SS_OP_POH_BOARD_REMOVE:
        SSVM_PushInt(state,
                     poh_board_remove_at(srv, poh_board_find(srv, player->display_name)));
        return 1;

    case SS_OP_POH_BOARD_COUNT:
        poh_board_prune(srv);
        SSVM_PushInt(state, srv->poh_board_count);
        return 1;

    case SS_OP_POH_BOARD_GET:
    {
        int32_t row;
        int32_t field;

        if( !SSVM_PopInt(state, &field) || !SSVM_PopInt(state, &row) )
            return 1;
        assert(row >= 0);
        assert(row < srv->poh_board_count);
        assert(field >= 0);
        assert(field <= TORIRSSERVER_POH_BOARD_FIELDS + 1);
        if( field < TORIRSSERVER_POH_BOARD_FIELDS )
            SSVM_PushInt(state, srv->poh_board[row].fields[field]);
        else if( field == TORIRSSERVER_POH_BOARD_FIELDS )
            SSVM_PushInt(state, srv->poh_board[row].expires_tick - srv->tick);
        else
            SSVM_PushInt(state,
                         strcmp(srv->poh_board[row].name, player->display_name) == 0);
        return 1;
    }

    case SS_OP_POH_BOARD_NAME:
    {
        int32_t row;

        if( !SSVM_PopInt(state, &row) )
            return 1;
        assert(row >= 0);
        assert(row < srv->poh_board_count);
        SSVM_PushStr(state, srv->poh_board[row].name);
        return 1;
    }

    case SS_OP_POH_VISIT_LAST_SET:
    {
        const char* name;

        if( !SSVM_PopStr(state, &name) )
            return 1;
        snprintf(player->poh_visit_last, sizeof(player->poh_visit_last), "%s", name);
        return 1;
    }

    case SS_OP_POH_VISIT_LAST_GET:
        SSVM_PushStr(state, player->poh_visit_last);
        return 1;

    default:
        return 0;
    }
}
