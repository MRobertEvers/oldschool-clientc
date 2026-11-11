/*
 * The command wire codec: one Editor_Cmd to bytes and back, in both forms.
 *
 * Its own unit because it is WIRE code, not server code. Every participant
 * needs it — the server to broadcast commands, every client to send and to
 * decode the echoes — but a client needs nothing else the server has. Keeping
 * it here is what lets a controller connection (torirs_mapedctl_main.c) link
 * the protocol without linking a document, a content tree, or a server.
 *
 * v1 is the fixed 144-byte square form, kept for v1 peers. v2 is the
 * kind-tagged variable form of MAPED_IMPLEMENTATION.md section 2.
 */

#include "torirs_maped.h"

#include <assert.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Shared field codecs                                                 */
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
    tile->underlay_id = (uint16_t)ToriRSMapEd_ReadU32(body + 28);
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

/* ------------------------------------------------------------------ */
/* v1: 144 bytes, squares only                                         */
/* ------------------------------------------------------------------ */

int
ToriRSMapEd_CmdEncodeV1(
    const struct Editor_Cmd* command,
    uint8_t out[TORIRSMAPED_CMD_WIRE_V1])
{
    int map_x;
    int map_z;

    assert(command);
    assert(out);

    if( command->doc_kind != EDITOR_DOC_SQUARE
        || (command->kind != EDITOR_CMD_TILE && command->kind != EDITOR_CMD_LOC)
        || !Editor_SquareKeyParse(command->doc_key, &map_x, &map_z) )
        return 0;

    memset(out, 0, TORIRSMAPED_CMD_WIRE_V1);
    ToriRSMapEd_WriteU32(out, (uint32_t)command->kind);
    ToriRSMapEd_WriteU32(out + 4, (uint32_t)map_x);
    ToriRSMapEd_WriteU32(out + 8, (uint32_t)map_z);
    if( command->kind == EDITOR_CMD_TILE )
    {
        ToriRSMapEd_WriteU32(out + 12, (uint32_t)command->u.tile.level);
        ToriRSMapEd_WriteU32(out + 16, (uint32_t)command->u.tile.x);
        ToriRSMapEd_WriteU32(out + 20, (uint32_t)command->u.tile.z);
        encode_tile(out + 24, &command->u.tile.before);
        encode_tile(out + 56, &command->u.tile.after);
    }
    else
    {
        encode_loc(out + 88, &command->u.loc.before);
        encode_loc(out + 112, &command->u.loc.after);
    }
    ToriRSMapEd_WriteU32(out + 136, (uint32_t)command->has_before);
    ToriRSMapEd_WriteU32(out + 140, (uint32_t)command->has_after);
    return 1;
}

int
ToriRSMapEd_CmdDecodeV1(
    const uint8_t* body,
    uint32_t length,
    struct Editor_Cmd* out_command)
{
    uint32_t kind;

    assert(body || length == 0);
    assert(out_command);

    if( length != TORIRSMAPED_CMD_WIRE_V1 )
        return 0;
    kind = ToriRSMapEd_ReadU32(body);
    if( kind != EDITOR_CMD_TILE && kind != EDITOR_CMD_LOC )
        return 0;

    Editor_CmdInitSquare(
        out_command,
        (enum Editor_CmdKind)kind,
        (int)ToriRSMapEd_ReadU32(body + 4),
        (int)ToriRSMapEd_ReadU32(body + 8));
    if( kind == EDITOR_CMD_TILE )
    {
        out_command->u.tile.level = (int)ToriRSMapEd_ReadU32(body + 12);
        out_command->u.tile.x = (int)ToriRSMapEd_ReadU32(body + 16);
        out_command->u.tile.z = (int)ToriRSMapEd_ReadU32(body + 20);
        decode_tile(body + 24, &out_command->u.tile.before);
        decode_tile(body + 56, &out_command->u.tile.after);
    }
    else
    {
        decode_loc(body + 88, &out_command->u.loc.before);
        decode_loc(body + 112, &out_command->u.loc.after);
    }
    out_command->has_before = (int)ToriRSMapEd_ReadU32(body + 136);
    out_command->has_after = (int)ToriRSMapEd_ReadU32(body + 140);
    return 1;
}

/* ------------------------------------------------------------------ */
/* v2: kind-tagged, variable                                           */
/* ------------------------------------------------------------------ */

/* A SPAWN_EDIT row: u32 is_obj, u32 suppress, string name, u32 abs_x,
 * u32 abs_z, u32 level, u32 count. */
static void
encode_spawn_row(
    struct ToriRSMapEdBuf* out,
    const struct Editor_CmdSpawnRow* row)
{
    ToriRSMapEd_PutU32(out, (uint32_t)row->is_obj);
    ToriRSMapEd_PutU32(out, (uint32_t)row->suppress);
    ToriRSMapEd_PutString(out, row->name);
    ToriRSMapEd_PutU32(out, (uint32_t)row->abs_x);
    ToriRSMapEd_PutU32(out, (uint32_t)row->abs_z);
    ToriRSMapEd_PutU32(out, (uint32_t)row->level);
    ToriRSMapEd_PutU32(out, (uint32_t)row->count);
}

static void
decode_spawn_row(
    struct ToriRSMapEdReader* reader,
    struct Editor_CmdSpawnRow* row)
{
    row->is_obj = ToriRSMapEd_GetU32(reader) ? 1 : 0;
    row->suppress = ToriRSMapEd_GetU32(reader) ? 1 : 0;
    ToriRSMapEd_GetString(reader, row->name, sizeof(row->name));
    row->abs_x = (int)ToriRSMapEd_GetU32(reader);
    row->abs_z = (int)ToriRSMapEd_GetU32(reader);
    row->level = (int)ToriRSMapEd_GetU32(reader);
    row->count = (int)ToriRSMapEd_GetU32(reader);
}

static void
encode_payload(
    const struct Editor_Cmd* command,
    struct ToriRSMapEdBuf* out)
{
    switch( command->kind )
    {
    case EDITOR_CMD_TILE:
    {
        uint8_t tiles[64];
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.tile.level);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.tile.x);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.tile.z);
        encode_tile(tiles, &command->u.tile.before);
        encode_tile(tiles + 32, &command->u.tile.after);
        ToriRSMapEd_BufWrite(out, tiles, sizeof(tiles));
        return;
    }
    case EDITOR_CMD_LOC:
    {
        uint8_t locs[48];
        encode_loc(locs, &command->u.loc.before);
        encode_loc(locs + 24, &command->u.loc.after);
        ToriRSMapEd_BufWrite(out, locs, sizeof(locs));
        return;
    }
    case EDITOR_CMD_SPAWN:
    case EDITOR_CMD_SPAWN_SUPPRESS:
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.spawn.is_obj);
        ToriRSMapEd_PutString(out, command->u.spawn.name);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.spawn.abs_x);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.spawn.abs_z);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.spawn.level);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.spawn.count);
        return;
    case EDITOR_CMD_SPAWN_EDIT:
        /* before row, after row, string before_text ("" = none) */
        encode_spawn_row(out, &command->u.spawn_edit.before);
        encode_spawn_row(out, &command->u.spawn_edit.after);
        ToriRSMapEd_PutString(out, command->u.spawn_edit.before_text);
        return;
    case EDITOR_CMD_CFG_FIELD:
        ToriRSMapEd_PutString(out, command->u.cfg_field.key);
        ToriRSMapEd_PutString(out, command->u.cfg_field.before);
        ToriRSMapEd_PutString(out, command->u.cfg_field.after);
        return;
    case EDITOR_CMD_CFG_BLOCK:
        ToriRSMapEd_PutString(out, command->u.cfg_block.name);
        ToriRSMapEd_PutString(out, command->u.cfg_block.new_name);
        return;
    case EDITOR_CMD_IF_FIELD:
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.if_field.component);
        ToriRSMapEd_PutString(out, command->u.if_field.key);
        ToriRSMapEd_PutString(out, command->u.if_field.before);
        ToriRSMapEd_PutString(out, command->u.if_field.after);
        return;
    case EDITOR_CMD_IF_COMPONENT:
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.if_component.component);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.if_component.parent_before);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.if_component.parent_after);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.if_component.index_before);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.if_component.index_after);
        return;
    case EDITOR_CMD_TEXT_SPLICE:
        assert(command->u.text_splice.before_len <= EDITOR_CMD_SPLICE_MAX);
        assert(command->u.text_splice.after_len
               <= EDITOR_CMD_SPLICE_MAX - command->u.text_splice.before_len);
        ToriRSMapEd_PutU32(out, command->u.text_splice.offset);
        ToriRSMapEd_PutU32(out, command->u.text_splice.before_len);
        ToriRSMapEd_PutU32(out, command->u.text_splice.after_len);
        ToriRSMapEd_BufWrite(
            out,
            (const uint8_t*)command->u.text_splice.text,
            (int)(command->u.text_splice.before_len + command->u.text_splice.after_len));
        return;
    case EDITOR_CMD_AREA_SET:
    case EDITOR_CMD_AREA_REMOVE:
    case EDITOR_CMD_COLLECTION_SET:
    case EDITOR_CMD_COLLECTION_REMOVE:
    case EDITOR_CMD_PALETTE_SET:
    case EDITOR_CMD_PALETTE_REMOVE:
    case EDITOR_CMD_SCENE_SET:
    case EDITOR_CMD_SCENE_REMOVE:
        /* i32 line, string before ("" = none), string after ("" = none):
         * the record's line before and after, editor_areas.h. */
        assert(command->u.area.before_len <= EDITOR_CMD_SPLICE_MAX / 2);
        assert(command->u.area.after_len <= EDITOR_CMD_SPLICE_MAX / 2);
        ToriRSMapEd_PutU32(out, (uint32_t)command->u.area.line);
        ToriRSMapEd_PutBytes(out, command->u.area.text, command->u.area.before_len);
        ToriRSMapEd_PutBytes(
            out, command->u.area.text + command->u.area.before_len, command->u.area.after_len);
        return;
    }
    assert(0 && "Editor_Cmd kind with no wire form");
}

void
ToriRSMapEd_CmdEncode(
    const struct Editor_Cmd* command,
    struct ToriRSMapEdBuf* out)
{
    struct ToriRSMapEdBuf payload;

    assert(command);
    assert(out);
    assert(strlen(command->doc_key) < EDITOR_DOC_KEY_MAX);

    memset(&payload, 0, sizeof(payload));
    encode_payload(command, &payload);

    ToriRSMapEd_PutU32(out, (uint32_t)command->doc_kind);
    ToriRSMapEd_PutString(out, command->doc_key);
    ToriRSMapEd_PutU32(out, (uint32_t)command->kind);
    ToriRSMapEd_PutU32(out, (uint32_t)command->has_before);
    ToriRSMapEd_PutU32(out, (uint32_t)command->has_after);
    ToriRSMapEd_PutBytes(
        out, ToriRSMapEd_BufPeek(&payload), (uint32_t)ToriRSMapEd_BufAvailable(&payload));
    ToriRSMapEd_BufFree(&payload);
}

static int
decode_payload(
    struct Editor_Cmd* command,
    const uint8_t* body,
    uint32_t length)
{
    struct ToriRSMapEdReader reader;

    ToriRSMapEd_ReaderInit(&reader, body, length);
    switch( command->kind )
    {
    case EDITOR_CMD_TILE:
        if( length != 12 + 64 )
            return 0;
        command->u.tile.level = (int)ToriRSMapEd_GetU32(&reader);
        command->u.tile.x = (int)ToriRSMapEd_GetU32(&reader);
        command->u.tile.z = (int)ToriRSMapEd_GetU32(&reader);
        decode_tile(body + 12, &command->u.tile.before);
        decode_tile(body + 44, &command->u.tile.after);
        return 1;
    case EDITOR_CMD_LOC:
        if( length != 48 )
            return 0;
        decode_loc(body, &command->u.loc.before);
        decode_loc(body + 24, &command->u.loc.after);
        return 1;
    case EDITOR_CMD_SPAWN:
    case EDITOR_CMD_SPAWN_SUPPRESS:
        command->u.spawn.is_obj = (int)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, command->u.spawn.name, sizeof(command->u.spawn.name));
        command->u.spawn.abs_x = (int)ToriRSMapEd_GetU32(&reader);
        command->u.spawn.abs_z = (int)ToriRSMapEd_GetU32(&reader);
        command->u.spawn.level = (int)ToriRSMapEd_GetU32(&reader);
        command->u.spawn.count = (int)ToriRSMapEd_GetU32(&reader);
        break;
    case EDITOR_CMD_SPAWN_EDIT:
        decode_spawn_row(&reader, &command->u.spawn_edit.before);
        decode_spawn_row(&reader, &command->u.spawn_edit.after);
        ToriRSMapEd_GetString(
            &reader, command->u.spawn_edit.before_text, sizeof(command->u.spawn_edit.before_text));
        break;
    case EDITOR_CMD_CFG_FIELD:
        ToriRSMapEd_GetString(&reader, command->u.cfg_field.key, sizeof(command->u.cfg_field.key));
        ToriRSMapEd_GetString(
            &reader, command->u.cfg_field.before, sizeof(command->u.cfg_field.before));
        ToriRSMapEd_GetString(
            &reader, command->u.cfg_field.after, sizeof(command->u.cfg_field.after));
        break;
    case EDITOR_CMD_CFG_BLOCK:
        ToriRSMapEd_GetString(
            &reader, command->u.cfg_block.name, sizeof(command->u.cfg_block.name));
        ToriRSMapEd_GetString(
            &reader, command->u.cfg_block.new_name, sizeof(command->u.cfg_block.new_name));
        break;
    case EDITOR_CMD_IF_FIELD:
        command->u.if_field.component = (int)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, command->u.if_field.key, sizeof(command->u.if_field.key));
        ToriRSMapEd_GetString(
            &reader, command->u.if_field.before, sizeof(command->u.if_field.before));
        ToriRSMapEd_GetString(
            &reader, command->u.if_field.after, sizeof(command->u.if_field.after));
        break;
    case EDITOR_CMD_IF_COMPONENT:
        command->u.if_component.component = (int)ToriRSMapEd_GetU32(&reader);
        command->u.if_component.parent_before = (int)ToriRSMapEd_GetU32(&reader);
        command->u.if_component.parent_after = (int)ToriRSMapEd_GetU32(&reader);
        command->u.if_component.index_before = (int)ToriRSMapEd_GetU32(&reader);
        command->u.if_component.index_after = (int)ToriRSMapEd_GetU32(&reader);
        break;
    case EDITOR_CMD_TEXT_SPLICE:
    {
        uint32_t before_len;
        uint32_t after_len;

        command->u.text_splice.offset = ToriRSMapEd_GetU32(&reader);
        before_len = ToriRSMapEd_GetU32(&reader);
        after_len = ToriRSMapEd_GetU32(&reader);
        if( reader.bad || before_len > EDITOR_CMD_SPLICE_MAX
            || after_len > EDITOR_CMD_SPLICE_MAX - before_len
            || length - reader.at != before_len + after_len )
            return 0;
        command->u.text_splice.before_len = before_len;
        command->u.text_splice.after_len = after_len;
        memcpy(command->u.text_splice.text, body + reader.at, before_len + after_len);
        return 1;
    }
    case EDITOR_CMD_AREA_SET:
    case EDITOR_CMD_AREA_REMOVE:
    case EDITOR_CMD_COLLECTION_SET:
    case EDITOR_CMD_COLLECTION_REMOVE:
    case EDITOR_CMD_PALETTE_SET:
    case EDITOR_CMD_PALETTE_REMOVE:
    case EDITOR_CMD_SCENE_SET:
    case EDITOR_CMD_SCENE_REMOVE:
    {
        const uint8_t* before;
        const uint8_t* after;
        uint32_t before_len = 0;
        uint32_t after_len = 0;

        command->u.area.line = (int)ToriRSMapEd_GetU32(&reader);
        before = ToriRSMapEd_GetBytes(&reader, &before_len);
        after = ToriRSMapEd_GetBytes(&reader, &after_len);
        if( reader.bad || before_len > EDITOR_CMD_SPLICE_MAX / 2 || after_len > EDITOR_CMD_SPLICE_MAX / 2 )
            return 0;
        command->u.area.before_len = before_len;
        command->u.area.after_len = after_len;
        if( before_len > 0 )
            memcpy(command->u.area.text, before, before_len);
        if( after_len > 0 )
            memcpy(command->u.area.text + before_len, after, after_len);
        break;
    }
    }
    return ToriRSMapEd_ReaderDone(&reader);
}

enum ToriRSMapEdCmdDecodeResult
ToriRSMapEd_CmdDecode(
    const uint8_t* body,
    uint32_t length,
    struct Editor_Cmd* out_command)
{
    struct ToriRSMapEdReader reader;
    uint32_t doc_kind;
    uint32_t kind;
    uint32_t payload_length;
    const uint8_t* payload;

    assert(body || length == 0);
    assert(out_command);

    memset(out_command, 0, sizeof(*out_command));
    ToriRSMapEd_ReaderInit(&reader, body, length);

    doc_kind = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, out_command->doc_key, sizeof(out_command->doc_key));
    kind = ToriRSMapEd_GetU32(&reader);
    out_command->has_before = ToriRSMapEd_GetU32(&reader) ? 1 : 0;
    out_command->has_after = ToriRSMapEd_GetU32(&reader) ? 1 : 0;
    payload = ToriRSMapEd_GetBytes(&reader, &payload_length);
    if( !ToriRSMapEd_ReaderDone(&reader) || doc_kind >= EDITOR_DOC_KIND_COUNT )
        return TORIRSMAPED_CMD_DECODE_MALFORMED;

    out_command->doc_kind = (enum Editor_DocKind)doc_kind;
    if( kind >= EDITOR_CMD_KIND_COUNT )
        return TORIRSMAPED_CMD_DECODE_UNKNOWN_KIND;
    out_command->kind = (enum Editor_CmdKind)kind;

    if( !decode_payload(out_command, payload, payload_length) )
        return TORIRSMAPED_CMD_DECODE_MALFORMED;
    return TORIRSMAPED_CMD_DECODE_OK;
}
