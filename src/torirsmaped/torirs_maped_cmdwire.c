/*
 * The command wire codec: one Editor_Cmd to bytes and back.
 *
 * Its own unit because it is WIRE code, not server code. Every participant
 * needs it — the server to broadcast commands, every client to send and to
 * decode the echoes — but a client needs nothing else the server has. Keeping
 * it here is what lets a controller connection (torirs_mapedctl_main.c) link
 * the protocol without linking a document, a content tree, or a server.
 */

#include "torirs_maped.h"

#include <assert.h>
#include <string.h>

/* ------------------------------------------------------------------ */

static void
encode_tile(
    uint8_t* out,
    const struct Editor_Tile* tile)
{
    ToriRSMapEd_WriteU32(out, tile->has_height);
    ToriRSMapEd_WriteU32(out + 4, tile->height);
    ToriRSMapEd_WriteU32(out + 8, tile->has_overlay);
    ToriRSMapEd_WriteU32(out + 12, tile->overlay_id);
    ToriRSMapEd_WriteU32(out + 16, tile->shape);
    ToriRSMapEd_WriteU32(out + 20, tile->rotation);
    ToriRSMapEd_WriteU32(out + 24, tile->settings);
    ToriRSMapEd_WriteU32(out + 28, tile->underlay_id);
}

static void
decode_tile(
    const uint8_t* body,
    struct Editor_Tile* tile)
{
    tile->has_height = (uint8_t)ToriRSMapEd_ReadU32(body);
    tile->height = (uint8_t)ToriRSMapEd_ReadU32(body + 4);
    tile->has_overlay = (uint8_t)ToriRSMapEd_ReadU32(body + 8);
    tile->overlay_id = (uint16_t)ToriRSMapEd_ReadU32(body + 12);
    tile->shape = (uint8_t)ToriRSMapEd_ReadU32(body + 16);
    tile->rotation = (uint8_t)ToriRSMapEd_ReadU32(body + 20);
    tile->settings = (uint8_t)ToriRSMapEd_ReadU32(body + 24);
    tile->underlay_id = (uint8_t)ToriRSMapEd_ReadU32(body + 28);
}

static void
encode_loc(
    uint8_t* out,
    const struct Editor_Loc* loc)
{
    ToriRSMapEd_WriteU32(out, (uint32_t)loc->loc_id);
    ToriRSMapEd_WriteU32(out + 4, (uint32_t)loc->shape);
    ToriRSMapEd_WriteU32(out + 8, (uint32_t)loc->rotation);
    ToriRSMapEd_WriteU32(out + 12, (uint32_t)loc->level);
    ToriRSMapEd_WriteU32(out + 16, (uint32_t)loc->x);
    ToriRSMapEd_WriteU32(out + 20, (uint32_t)loc->z);
}

static void
decode_loc(
    const uint8_t* body,
    struct Editor_Loc* loc)
{
    loc->loc_id = (int)ToriRSMapEd_ReadU32(body);
    loc->shape = (int)ToriRSMapEd_ReadU32(body + 4);
    loc->rotation = (int)ToriRSMapEd_ReadU32(body + 8);
    loc->level = (int)ToriRSMapEd_ReadU32(body + 12);
    loc->x = (int)ToriRSMapEd_ReadU32(body + 16);
    loc->z = (int)ToriRSMapEd_ReadU32(body + 20);
}

void
ToriRSMapEd_CmdEncode(
    const struct Editor_Cmd* command,
    uint8_t out[TORIRSMAPED_CMD_WIRE])
{
    assert(command);
    assert(out);

    ToriRSMapEd_WriteU32(out, (uint32_t)command->kind);
    ToriRSMapEd_WriteU32(out + 4, (uint32_t)command->map_x);
    ToriRSMapEd_WriteU32(out + 8, (uint32_t)command->map_z);
    ToriRSMapEd_WriteU32(out + 12, (uint32_t)command->level);
    ToriRSMapEd_WriteU32(out + 16, (uint32_t)command->x);
    ToriRSMapEd_WriteU32(out + 20, (uint32_t)command->z);
    encode_tile(out + 24, &command->tile_before);
    encode_tile(out + 56, &command->tile_after);
    encode_loc(out + 88, &command->loc_before);
    encode_loc(out + 112, &command->loc_after);
    ToriRSMapEd_WriteU32(out + 136, (uint32_t)command->has_before);
    ToriRSMapEd_WriteU32(out + 140, (uint32_t)command->has_after);
}

int
ToriRSMapEd_CmdDecode(
    const uint8_t* body,
    uint32_t length,
    struct Editor_Cmd* out_command)
{
    uint32_t kind;

    assert(body || length == 0);
    assert(out_command);

    if( length != TORIRSMAPED_CMD_WIRE )
        return 0;
    kind = ToriRSMapEd_ReadU32(body);
    if( kind != EDITOR_CMD_TILE && kind != EDITOR_CMD_LOC )
        return 0;

    memset(out_command, 0, sizeof(*out_command));
    out_command->kind = (enum Editor_CmdKind)kind;
    out_command->map_x = (int)ToriRSMapEd_ReadU32(body + 4);
    out_command->map_z = (int)ToriRSMapEd_ReadU32(body + 8);
    out_command->level = (int)ToriRSMapEd_ReadU32(body + 12);
    out_command->x = (int)ToriRSMapEd_ReadU32(body + 16);
    out_command->z = (int)ToriRSMapEd_ReadU32(body + 20);
    decode_tile(body + 24, &out_command->tile_before);
    decode_tile(body + 56, &out_command->tile_after);
    decode_loc(body + 88, &out_command->loc_before);
    decode_loc(body + 112, &out_command->loc_after);
    out_command->has_before = (int)ToriRSMapEd_ReadU32(body + 136);
    out_command->has_after = (int)ToriRSMapEd_ReadU32(body + 140);
    return 1;
}

