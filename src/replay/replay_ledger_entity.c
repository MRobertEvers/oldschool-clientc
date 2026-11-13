/*
 * The entity half of the world ledger: the player decoder's table, the
 * high-resolution players and the npc list, folded through decoded info op
 * lists. One step function (ledger_entity_step) builds the keys and serves
 * seeks, so a key advanced to P and a fold from tick 0 to P are the same code.
 *
 * ## What persists on an entity: the sticky / transient split
 *
 * Read off how the executor consumes each op (src/game/task_exec_entity_info.c;
 * line numbers as of d4e6b1b0d). "Sticky" stays on the entity until another op
 * replaces it; "transient" is a one-shot effect a 16-tick pre-roll re-creates
 * or that has expired; "position" is carried as the tile; "not consumed" is an
 * op the executor's switch drops (its default arm), so a keyframe has nothing
 * to give it.
 *
 *   op                  player (player_apply_op :552)   npc (npc_apply_op :1444)
 *   ------------------  ------------------------------  ------------------------------
 *   APPEARANCE          sticky :685 (APPEARANCE)        -
 *   NPCTYPE             -                               sticky :1455 (type field)
 *   CHANGE_TYPE         -                               sticky :1628 (type field)
 *   FACE_ENTITY         sticky :712 (FACE_ENTITY)       sticky :1531 (FACE_ENTITY)
 *   FACE_COORD          sticky :732 (FACE_DIR)          sticky :1554 (FACE_DIR)
 *   FACE_ANGLE          sticky :920 (FACE_DIR)          sticky :1680 (FACE_DIR; a spawn
 *                                                       facing on a re-added npc is
 *                                                       skipped there, and here)
 *   SPAWN_CYCLE         -                               sticky :1693
 *   VISIBLE_OPS         -                               sticky :1701
 *   NAME_CHANGE         -                               sticky :1709
 *   LEVEL_CHANGE        -                               sticky :1718
 *   HEADICON_CUSTOM     -                               sticky :1726
 *   BODY_CUSTOM         -                               sticky :1740
 *   BAS_CHANGE          -                               sticky :1768
 *   SEQUENCE            transient :703                  transient :1519
 *   SAY / CHAT          transient :749 / :768           transient :1587
 *   DAMAGE / HEADBAR    transient :807 / :833           transient :1592 / :1612
 *   SPOTANIM            transient :848                  transient :1635
 *   EXACT_MOVE          transient :867                  transient :1650
 *   TINT / TRANSPARENCY transient :905 / :925           transient :1745 / :1757
 *                       (timed: the design's "aged sticky blocks", stage 3)
 *   WALK/RUN/CRAWLDIR,  position :566-:684              position :1471-:1518
 *   ABS/LOCAL_XZLEVEL,
 *   DELTA_XZ
 *   MOVE_SPEED,         not consumed (the reader folds  not consumed
 *   TEMP_MOVE_SPEED     it into ABS_XZLEVEL from the
 *                       table's move_speed, which the
 *                       key carries in the table bytes)
 *   NAME_EXTRAS, RESET, not consumed                    not consumed
 *   FREEZE, HEAD_CUSTOM
 *
 * Within one sticky slot the latest op replaces the earlier (each setter writes
 * the whole field: World_NpcSetBodyModels, World_NpcSetHeadiconCustom, the BAS
 * block's full set at :1776). A keyframe hands an entity's sticky ops over in
 * recorded order, so FACE_ENTITY against FACE_DIR lands as it did.
 *
 * NPC_HEADICON_SPECIFIC is not an info op but a packet of its own that writes
 * ONE head-icon slot of an npc this client tracks (exec_npc_headicon_specific,
 * rs_gameproto_exec.c: an npc not in the registry is skipped, a slot outside
 * 0..7 refused; the first write copies the type's icons in). So it gets a
 * sticky slot per head-icon slot, set from the npc list as it stands when the
 * packet arrives, and a HEADICON_CUSTOM, which writes every slot, clears them.
 * The keyframe hands them over as packets, applied after the npcs stand.
 *
 * ## The npc list
 *
 * NPC_INFO addresses the client's list by position, so the order is state
 * (rs_entity_sync.h, the INVARIANT note). The rebuild here follows
 * Task_ExecNpcInfo_Run (:1851-:2003) and npc_target_op (:1285): COUNT_RESET
 * empties the list (entries past the count are gone), ADD_OLD appends the old
 * entry at that position, CLEAR_OLD parks it (dropped at the packet's end
 * unless an ADD_NEW of the same slot revives it, :1354), ADD_NEW appends a new
 * one, SET_EXT addresses the rebuilt list by position. An ADD_OLD past the old
 * list appends -1 (:1316), which every op then ignores.
 *
 * Tiles are absolute. A new npc is placed at the origin (npc_spawn_now :1224,
 * RS_EntityInfo_NpcOrigin :1158: SET_NPC_UPDATE_ORIGIN when one was sent, else
 * the local player's tile; the origin's level), DELTA_XZ jumps it to origin +
 * delta (:1498), and steps move it by World_CoordStep's table
 * (world/entity_pathing.c:141).
 *
 * ## Players
 *
 * The OSRS player stream addresses its 2048-slot table by slot, and the table
 * holds each high-resolution player's tile; the executor keys players by pid
 * (player_target_op :391). So the players of a key are the table's high_res
 * slots, and the fold only keeps their sticky references. REMOVE_PID (:444)
 * drops a player and with it its sticky state.
 *
 * A RECONNECT re-seeds the table (the client's RECONNECT_OK: net.c
 * loginproto_drive, set_local then init from the block). The replay client's
 * session reset clears its entity lists there too (App_NetSessionReset), and
 * the table it will decode against holds only the local player in high
 * resolution: the keyframe is the server's view, so every other player's
 * sticky state goes with the re-seed.
 */
#include "replay/replay_ledger_internal.h"

#include "net/rev/pktnames.h"
#include "rsprot_buf.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEDGER_INFO_OPS 16384
#define LEDGER_INFO_BYTES (1 << 18)

/* The decode lists. One build or seek at a time per process, as the client's
 * own info lists are one per App. */
static RsprotInfoOp g_ops[LEDGER_INFO_OPS];
static uint8_t g_bytes[LEDGER_INFO_BYTES];

/* ReplayLedger_EntityStepChat's collection, while it steps (NULL otherwise). */
static struct ReplayChatLine* g_chat_out;
static int g_chat_cap;
static int g_chat_count;

static void
sticky_clear(uint64_t* sticky, int count)
{
    for( int i = 0; i < count; i++ )
        sticky[i] = REPLAY_STICKY_NONE;
}

void
ledger_entity_reset(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState* state)
{
    assert(ledger);
    assert(state);
    memset(state, 0, sizeof(*state));
    rsprot_player_info_reset(&state->pi, ledger->rec->local_player_index);
    memset(state->player_sticky, 0xff, sizeof(state->player_sticky));
    state->active_root = 1;
}

void
ledger_entity_seed_table(
    struct ReplayEntityState* state,
    RsprotPlayerInfo const* seeded)
{
    int const local = seeded->local_index;

    assert(state);
    assert(seeded);
    /* A seed is a new table: only the local player is in high resolution. */
    for( int i = 0; i < REPLAY_LEDGER_PLAYER_SLOTS; i++ )
        if( i != local || state->pi.local_index != local )
            sticky_clear(state->player_sticky[i], REPLAY_PLAYER_STICKY_COUNT);
    memcpy(&state->pi, seeded, sizeof(state->pi));
}

/* --- player sticky copies (build only) ---------------------------------- */

static int
find_bytes(
    uint8_t const* hay,
    int hay_len,
    uint8_t const* needle,
    int len)
{
    if( len <= 0 || len > hay_len )
        return -1;
    for( int i = 0; i + len <= hay_len; i++ )
        if( hay[i] == needle[0] && memcmp(hay + i, needle, (size_t)len) == 0 )
            return i;
    return -1;
}

/* An appearance blob as the reader hands it over (natural order) and as the
 * wire carries it: one of the four gdata orders (rsprot_info_blob,
 * streams/rsprot_info_blocks.c:114). +128 and -128 are the same byte. */
static int const k_blob_orders[4] = {
    RSPROT_IP_DATA, RSPROT_IP_DATA_REV, RSPROT_IP_DATA_ADD128, RSPROT_IP_DATA_REV_ADD128
};

static void
blob_wire(
    uint8_t const* natural,
    int len,
    int order,
    uint8_t* out)
{
    int rev = order == RSPROT_IP_DATA_REV || order == RSPROT_IP_DATA_REV_ADD128;
    int add = order == RSPROT_IP_DATA_ADD128 || order == RSPROT_IP_DATA_REV_ADD128;

    for( int i = 0; i < len; i++ )
        out[i] = (uint8_t)(natural[rev ? len - 1 - i : i] ^ (add ? 0x80 : 0));
}

/* The reader's own transform, from the wire bytes at `src`. */
static void
blob_natural(
    uint8_t const* src,
    int len,
    int order,
    uint8_t* out)
{
    RsprotBuf b;

    rsprot_buf_wrap_read(&b, src, len);
    if( order == RSPROT_IP_DATA_REV )
        rsprot_gdata_rev(&b, out, len);
    else if( order == RSPROT_IP_DATA_ADD128 )
        rsprot_gdata_add128(&b, out, len);
    else if( order == RSPROT_IP_DATA_REV_ADD128 )
        rsprot_gdata_rev_add128(&b, out, len);
    else
        rsprot_gdata(&b, out, len);
}

static void
record_player_op(
    struct ReplayLedger* building,
    uint32_t f,
    int op_index,
    RsprotInfoOp const* op)
{
    struct ReplayLedgerPlayerOp* copy;
    struct RsproxRecordingFrame const* fr = &building->rec->frames[f];
    uint8_t const* payload = building->rec->data + fr->offset;
    int len = op->appearance.len;

    if( building->player_op_count == building->player_op_cap )
    {
        building->player_op_cap = building->player_op_cap ? building->player_op_cap * 2 : 1024;
        building->player_ops = realloc(
            building->player_ops, (size_t)building->player_op_cap * sizeof(*building->player_ops));
        assert(building->player_ops);
    }
    copy = &building->player_ops[building->player_op_count++];
    memset(copy, 0, sizeof(*copy));
    copy->ref = REPLAY_STICKY_REF(f, op_index);
    copy->op = *op;
    copy->blob_order = -1;
    if( op->kind != RSPROT_OP_APPEARANCE )
        return;
    copy->op.appearance.appearance = NULL;
    copy->blob_len = len;
    if( len <= 0 || !op->appearance.appearance )
        return;
    /* Where it stands in the frame, in whichever order the block sent it:
     * a reference into the recording, not a copy. */
    {
        uint8_t* wire = malloc((size_t)len);
        uint8_t* back = malloc((size_t)len);

        assert(wire);
        assert(back);
        for( int k = 0; k < 4 && copy->blob_order < 0; k++ )
        {
            int at;

            blob_wire(op->appearance.appearance, len, k_blob_orders[k], wire);
            at = find_bytes(payload, (int)fr->length, wire, len);
            if( at < 0 )
                continue;
            blob_natural(payload + at, len, k_blob_orders[k], back);
            if( memcmp(back, op->appearance.appearance, (size_t)len) != 0 )
                continue;
            copy->blob_order = (int8_t)k_blob_orders[k];
            copy->blob_offset = fr->offset + (uint32_t)at;
        }
        free(wire);
        free(back);
    }
    if( copy->blob_order >= 0 )
    {
        building->stats.appearance_refs++;
        return;
    }
    building->stats.appearance_copies++;
    if( building->blob_used + (size_t)len > building->blob_cap )
    {
        while( building->blob_used + (size_t)len > building->blob_cap )
            building->blob_cap = building->blob_cap ? building->blob_cap * 2 : 65536;
        building->blobs = realloc(building->blobs, building->blob_cap);
        assert(building->blobs);
    }
    memcpy(building->blobs + building->blob_used, op->appearance.appearance, (size_t)len);
    copy->blob_offset = (uint32_t)building->blob_used;
    building->blob_used += (size_t)len;
}

/* --- PLAYER_INFO ---------------------------------------------------------- */

static void
player_info(
    struct ReplayLedger const* ledger,
    struct ReplayLedger* building,
    struct ReplayEntityState* state,
    uint32_t f)
{
    struct RsproxRecordingFrame const* fr = &ledger->rec->frames[f];
    RsprotInfoList list;
    int count;
    int cur = -1;

    rsprot_info_list_init(&list, g_ops, LEDGER_INFO_OPS, g_bytes, LEDGER_INFO_BYTES);
    count = ledger->player_read(&state->pi, ledger->rec->data + fr->offset, (int32_t)fr->length,
                                &list);
    if( building )
    {
        building->stats.player_info_frames++;
        if( !rsprot_info_list_complete(&list) )
            building->stats.player_info_incomplete++;
    }
    for( int i = 0; i < count; i++ )
    {
        RsprotInfoOp const* op = &list.ops[i];
        int slot = -1;

        switch( op->kind )
        {
        case RSPROT_OP_SET_LOCAL_PLAYER:
            cur = state->pi.local_index;
            continue;
        case RSPROT_OP_ADD_NEW:
            cur = (int)op->value;
            continue;
        case RSPROT_OP_REMOVE_PID:
            if( op->value < REPLAY_LEDGER_PLAYER_SLOTS )
                sticky_clear(state->player_sticky[op->value], REPLAY_PLAYER_STICKY_COUNT);
            cur = -1;
            continue;
        case RSPROT_OP_APPEARANCE:
            slot = REPLAY_PLAYER_APPEARANCE;
            break;
        case RSPROT_OP_SAY:
        case RSPROT_OP_CHAT:
            /* player_apply_op puts these in the chatbox; collected for a
             * chase that withholds the frame (ReplayLedger_EntityStepChat). */
            if( g_chat_out && cur >= 0 && cur < REPLAY_LEDGER_PLAYER_SLOTS &&
                g_chat_count < g_chat_cap &&
                (op->kind == RSPROT_OP_SAY ? op->say.text != NULL
                                           : op->chat.data != NULL && op->chat.length > 0) )
            {
                struct ReplayChatLine* line = &g_chat_out[g_chat_count++];

                memset(line, 0, sizeof(*line));
                line->pid = cur;
                line->say = op->kind == RSPROT_OP_SAY;
                line->text = line->say ? op->say.text : NULL;
                line->data = line->say ? NULL : op->chat.data;
                line->len = line->say ? 0 : op->chat.length;
                line->crown = line->say ? 0 : op->chat.type;
                line->colour_effect = line->say ? 0 : op->chat.colour_effect;
                line->appearance_ref = state->player_sticky[cur][REPLAY_PLAYER_APPEARANCE];
            }
            continue;
        case RSPROT_OP_FACE_ENTITY:
            slot = REPLAY_PLAYER_FACE_ENTITY;
            break;
        case RSPROT_OP_FACE_COORD:
        case RSPROT_OP_FACE_ANGLE:
            slot = REPLAY_PLAYER_FACE_DIR;
            break;
        default:
            continue;
        }
        if( cur < 0 || cur >= REPLAY_LEDGER_PLAYER_SLOTS )
            continue;
        state->player_sticky[cur][slot] = REPLAY_STICKY_REF(f, i);
        if( building )
            record_player_op(building, f, i, op);
    }
}

/* --- NPC_INFO --------------------------------------------------------------- */

/* World_CoordStep (world/entity_pathing.c:141): 0 north-west .. 7 south-east. */
static int const k_step_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static int const k_step_dz[8] = { 1, 1, 1, 0, 0, -1, -1, -1 };

/* RS_EntityInfo_NpcOrigin (task_exec_entity_info.c:1158), absolute. */
static void
npc_origin(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState const* state,
    uint32_t f,
    int* x,
    int* z,
    int* level)
{
    int32_t coord = 0;

    if( state->pi.local_index > 0 && state->pi.local_index < REPLAY_LEDGER_PLAYER_SLOTS )
        coord = state->pi.coord[state->pi.local_index];
    *x = REPLAY_TILE_X((uint32_t)coord);
    *z = REPLAY_TILE_Z((uint32_t)coord);
    *level = REPLAY_TILE_LEVEL((uint32_t)coord);
    if( state->npc_origin_valid )
    {
        int scene = ledger_scene_at(ledger, f);
        int base_x = scene >= 0 ? ledger->scenes[scene].base_x : 0;
        int base_z = scene >= 0 ? ledger->scenes[scene].base_z : 0;

        *x = base_x + state->npc_origin_x;
        *z = base_z + state->npc_origin_z;
    }
}

static void
npc_fresh(
    struct ReplayLedgerNpc* npc,
    int slot)
{
    memset(npc, 0, sizeof(*npc));
    npc->slot = slot;
    npc->type = REPLAY_NPC_TYPE_NONE;
    npc->spawn_type = REPLAY_NPC_TYPE_NONE;
    sticky_clear(npc->sticky, REPLAY_NPC_STICKY_COUNT);
}

static void
npc_info(
    struct ReplayLedger const* ledger,
    struct ReplayLedger* building,
    struct ReplayEntityState* state,
    uint32_t f,
    int large)
{
    static struct ReplayLedgerNpc old[REPLAY_LEDGER_MAX_NPCS];
    static int cleared[REPLAY_LEDGER_MAX_NPCS];
    struct RsproxRecordingFrame const* fr = &ledger->rec->frames[f];
    RsprotInfoList list;
    int count;
    int old_count = state->npc_count;
    int cleared_count = 0;
    int n = 0;
    int cur = -1;
    int fresh = 0;
    int revived = 0;

    rsprot_info_list_init(&list, g_ops, LEDGER_INFO_OPS, g_bytes, LEDGER_INFO_BYTES);
    if( large )
    {
        assert(ledger->rev->npc_info_read_large);
        count = ledger->rev->npc_info_read_large(ledger->rec->data + fr->offset, (int)fr->length,
                                                 &list);
    }
    else
    {
        assert(ledger->rev->npc_info_read);
        count = ledger->rev->npc_info_read(ledger->rec->data + fr->offset, (int)fr->length, &list);
    }
    if( building )
    {
        building->stats.npc_info_frames++;
        if( !rsprot_info_list_complete(&list) )
            building->stats.npc_info_incomplete++;
    }
    memcpy(old, state->npcs, (size_t)old_count * sizeof(old[0]));

    for( int i = 0; i < count; i++ )
    {
        RsprotInfoOp const* op = &list.ops[i];
        struct ReplayLedgerNpc* npc = cur >= 0 ? &state->npcs[cur] : NULL;
        int slot = -1;

        switch( op->kind )
        {
        case RSPROT_OP_COUNT_RESET:
            /* The list is rebuilt from `old`; entries past the count are not
             * walked and so are not carried. */
            n = 0;
            cur = -1;
            continue;
        case RSPROT_OP_ADD_OLD:
            if( n >= REPLAY_LEDGER_MAX_NPCS )
            {
                cur = -1;
                continue;
            }
            if( op->value < (uint64_t)old_count )
                state->npcs[n] = old[op->value];
            else
                npc_fresh(&state->npcs[n], -1);
            cur = n++;
            fresh = 0;
            revived = 0;
            continue;
        case RSPROT_OP_CLEAR_OLD:
            if( op->value < (uint64_t)old_count && old[op->value].slot >= 0 )
                cleared[cleared_count++] = (int)op->value;
            cur = -1;
            continue;
        case RSPROT_OP_ADD_NEW:
        {
            int at = -1;

            if( n >= REPLAY_LEDGER_MAX_NPCS )
            {
                cur = -1;
                continue;
            }
            for( int k = 0; k < cleared_count; k++ )
                if( old[cleared[k]].slot == (int)op->value )
                {
                    at = cleared[k];
                    cleared[k] = cleared[--cleared_count];
                    break;
                }
            if( at >= 0 )
                state->npcs[n] = old[at];
            else
                npc_fresh(&state->npcs[n], (int)op->value);
            revived = at >= 0;
            fresh = at < 0;
            cur = n++;
            continue;
        }
        case RSPROT_OP_SET_EXT:
            cur = op->value < (uint64_t)n ? (int)op->value : -1;
            fresh = 0;
            revived = 0;
            continue;
        case RSPROT_OP_INFO:
            continue;
        case RSPROT_OP_NPCTYPE:
            if( !npc || npc->slot < 0 )
                continue;
            if( fresh )
            {
                int x, z, level;

                npc_origin(ledger, state, f, &x, &z, &level);
                npc->x = x;
                npc->z = z;
                npc->level = level;
                npc->spawn_type = (int32_t)op->value;
            }
            npc->type = (int32_t)op->value;
            continue;
        case RSPROT_OP_CHANGE_TYPE:
            if( npc && npc->slot >= 0 )
                npc->type = op->change_type.npc_type;
            continue;
        case RSPROT_OP_WALKDIR:
        case RSPROT_OP_RUNDIR:
        case RSPROT_OP_CRAWLDIR:
            if( npc && npc->slot >= 0 && op->value < 8 )
            {
                npc->x += k_step_dx[op->value];
                npc->z += k_step_dz[op->value];
            }
            continue;
        case RSPROT_OP_DELTA_XZ:
            if( npc && npc->slot >= 0 )
            {
                int x, z, level;

                npc_origin(ledger, state, f, &x, &z, &level);
                npc->x = x + op->delta_xz.dx;
                npc->z = z + op->delta_xz.dz;
            }
            continue;
        case RSPROT_OP_FACE_ENTITY:
            slot = REPLAY_NPC_FACE_ENTITY;
            break;
        case RSPROT_OP_FACE_ANGLE:
            /* npc_apply_op :1681: a spawn facing does not turn a re-added npc. */
            if( op->face_angle.spawn && revived )
                continue;
            slot = REPLAY_NPC_FACE_DIR;
            break;
        case RSPROT_OP_FACE_COORD:
            slot = REPLAY_NPC_FACE_DIR;
            break;
        case RSPROT_OP_SPAWN_CYCLE:
            slot = REPLAY_NPC_SPAWN_CYCLE;
            break;
        case RSPROT_OP_VISIBLE_OPS:
            slot = REPLAY_NPC_VISIBLE_OPS;
            break;
        case RSPROT_OP_NAME_CHANGE:
            slot = REPLAY_NPC_NAME;
            break;
        case RSPROT_OP_LEVEL_CHANGE:
            slot = REPLAY_NPC_LEVEL;
            break;
        case RSPROT_OP_HEADICON_CUSTOM:
            slot = REPLAY_NPC_HEADICON;
            break;
        case RSPROT_OP_BODY_CUSTOM:
            slot = REPLAY_NPC_BODY;
            break;
        case RSPROT_OP_BAS_CHANGE:
            slot = REPLAY_NPC_BAS;
            break;
        default:
            continue;
        }
        if( npc && npc->slot >= 0 )
        {
            npc->sticky[slot] = REPLAY_STICKY_REF(f, i);
            /* The whole set, over any NPC_HEADICON_SPECIFIC before it. */
            if( slot == REPLAY_NPC_HEADICON )
                sticky_clear(npc->sticky + REPLAY_NPC_HEADICON_SPECIFIC, REPLAY_NPC_HEADICON_SLOTS);
        }
    }
    /* Removed and not added back: gone (:2001). */
    state->npc_count = n;
}

/* --- the step --------------------------------------------------------------- */

void
ledger_entity_step(
    struct ReplayLedger const* ledger,
    struct ReplayLedger* building,
    struct ReplayEntityState* state,
    uint32_t f)
{
    struct RsproxRecordingFrame const* fr;
    int name;

    assert(ledger);
    assert(state);
    assert(f == state->next_frame);
    assert(f < (uint32_t)ledger->rec->frame_count);
    fr = &ledger->rec->frames[f];
    state->next_frame = f + 1;

    if( fr->opcode == RSPROX_RECORDING_RECONNECT )
    {
        RsprotPlayerInfo seeded;
        int const index = fr->reconnect_index >= 0 ? fr->reconnect_index
                                                   : state->pi.local_index;

        /* RECONNECT_OK (net.c loginproto_drive): set_local, then init from
         * the block. */
        rsprot_player_info_reset(&seeded, index);
        rsprot_player_info_init(&seeded, ledger->rec->data + fr->offset, (int32_t)fr->length);
        ledger_entity_seed_table(state, &seeded);
        return;
    }
    name = ledger_frame_name(ledger, f);
    switch( name )
    {
    case PKT_NAME_REBUILD_NORMAL:
    {
        /* The login REBUILD carries the init block (proto_adapt.c
         * adapt_rebuild_normal); the build kept the table it seeds. */
        int scene = ledger_scene_at(ledger, f + 1);

        if( scene >= 0 && ledger->scenes[scene].frame == f && ledger->scenes[scene].login )
        {
            assert(ledger->login_tables);
            assert(ledger->login_tables[scene]);
            ledger_entity_seed_table(state, ledger->login_tables[scene]);
        }
        break;
    }
    case PKT_NAME_PLAYER_INFO:
        player_info(ledger, building, state, f);
        break;
    case PKT_NAME_NPC_INFO:
    case PKT_NAME_NPC_INFO_LARGE:
        /* NPC_INFO goes to the view SET_ACTIVE_WORLD named
         * (task_exec_entity_info.c:1851): a world entity's list is not the
         * root's, and is not folded. */
        if( !state->active_root )
        {
            if( building )
                building->stats.npc_info_world_entity++;
            break;
        }
        npc_info(ledger, building, state, f, name == PKT_NAME_NPC_INFO_LARGE);
        break;
    case PKT_NAME_SET_NPC_UPDATE_ORIGIN:
    {
        struct RevPacket pkt;

        /* rs_gameproto_exec.c:3729: kept, never cleared. */
        if( ledger_parse(ledger, f, &pkt) )
        {
            state->npc_origin_valid = 1;
            state->npc_origin_x = pkt._set_npc_update_origin.x;
            state->npc_origin_z = pkt._set_npc_update_origin.z;
            gameproto_free(&pkt);
        }
        break;
    }
    case PKT_NAME_SET_ACTIVE_WORLD:
    {
        struct RevPacket pkt;

        /* rs_gameproto_exec.c:3737; WORLDVIEW_WIRE_ROOT is -1. */
        if( ledger_parse(ledger, f, &pkt) )
        {
            state->active_root = pkt._set_active_world.world_id == -1;
            gameproto_free(&pkt);
        }
        break;
    }
    case PKT_NAME_NPC_HEADICON_SPECIFIC:
    {
        struct RevPacket pkt;

        /* exec_npc_headicon_specific: one slot of an npc in the registry. */
        if( ledger_parse(ledger, f, &pkt) )
        {
            struct PktNpcHeadiconSpecific const* h = &pkt._npc_headicon_specific;

            if( h->slot >= 0 && h->slot < REPLAY_NPC_HEADICON_SLOTS )
                for( int i = 0; i < state->npc_count; i++ )
                    if( state->npcs[i].slot >= 0 && state->npcs[i].slot == h->npc_index )
                    {
                        state->npcs[i].sticky[REPLAY_NPC_HEADICON_SPECIFIC + h->slot] =
                            REPLAY_STICKY_REF(f, 0);
                        break;
                    }
            gameproto_free(&pkt);
        }
        break;
    }
    case PKT_NAME_CLEAR_ENTITIES:
        /* rs_gameproto_exec.c:1062: the active world's npcs go, players stay,
         * and the cursor returns to the root. */
        if( state->active_root )
            state->npc_count = 0;
        state->active_root = 1;
        break;
    case PKT_NAME_SERVER_TICK_END:
        /* rs_gameproto_exec.c:3784: the cursor returns to the root. */
        state->active_root = 1;
        break;
    default:
        break;
    }
}

/* --- keys -------------------------------------------------------------------- */

void
ledger_key_take(
    struct ReplayEntityState const* state,
    struct ReplayLedgerKey* key)
{
    int players = 0;

    assert(state);
    assert(key);
    memset(key, 0, sizeof(*key));
    key->tick = state->tick;
    key->next_frame = state->next_frame;
    key->pi = malloc(sizeof(*key->pi));
    assert(key->pi);
    memcpy(key->pi, &state->pi, sizeof(*key->pi));
    for( int i = 0; i < REPLAY_LEDGER_PLAYER_SLOTS; i++ )
    {
        if( state->pi.high_res[i] )
        {
            players++;
            continue;
        }
        /* The invariant a key relies on: no sticky state off the table. */
        for( int s = 0; s < REPLAY_PLAYER_STICKY_COUNT; s++ )
            assert(state->player_sticky[i][s] == REPLAY_STICKY_NONE);
    }
    key->player_count = players;
    if( players > 0 )
    {
        key->players = malloc((size_t)players * sizeof(*key->players));
        assert(key->players);
    }
    players = 0;
    for( int i = 0; i < REPLAY_LEDGER_PLAYER_SLOTS; i++ )
    {
        if( !state->pi.high_res[i] )
            continue;
        key->players[players].pid = (uint16_t)i;
        memcpy(key->players[players].sticky, state->player_sticky[i],
               sizeof(key->players[players].sticky));
        players++;
    }
    key->npc_count = state->npc_count;
    if( state->npc_count > 0 )
    {
        key->npcs = malloc((size_t)state->npc_count * sizeof(*key->npcs));
        assert(key->npcs);
        memcpy(key->npcs, state->npcs, (size_t)state->npc_count * sizeof(*key->npcs));
    }
    key->npc_origin_valid = state->npc_origin_valid;
    key->npc_origin_x = state->npc_origin_x;
    key->npc_origin_z = state->npc_origin_z;
}

void
ledger_key_release(struct ReplayLedgerKey* key)
{
    if( !key )
        return;
    free(key->pi);
    free(key->players);
    free(key->npcs);
    memset(key, 0, sizeof(*key));
}

size_t
ledger_key_bytes(struct ReplayLedgerKey const* key)
{
    assert(key);
    return sizeof(*key) + sizeof(*key->pi) + (size_t)key->player_count * sizeof(*key->players) +
           (size_t)key->npc_count * sizeof(*key->npcs);
}

struct ReplayEntityState*
ReplayLedger_EntityNew(struct ReplayLedger const* ledger)
{
    struct ReplayEntityState* state;

    assert(ledger);
    state = malloc(sizeof(*state));
    assert(state);
    ledger_entity_reset(ledger, state);
    return state;
}

void
ReplayEntityState_Free(struct ReplayEntityState* state)
{
    free(state);
}

void
ReplayLedger_EntityLoadKey(
    struct ReplayLedger const* ledger,
    int key,
    struct ReplayEntityState* state)
{
    struct ReplayLedgerKey const* k;

    assert(ledger);
    assert(state);
    assert(key >= 0);
    assert(key < ledger->key_count);
    k = &ledger->keys[key];
    memset(state, 0, sizeof(*state));
    state->tick = k->tick;
    state->next_frame = k->next_frame;
    memcpy(&state->pi, k->pi, sizeof(state->pi));
    memset(state->player_sticky, 0xff, sizeof(state->player_sticky));
    for( int i = 0; i < k->player_count; i++ )
        memcpy(state->player_sticky[k->players[i].pid], k->players[i].sticky,
               sizeof(k->players[i].sticky));
    state->npc_count = k->npc_count;
    if( k->npc_count > 0 )
        memcpy(state->npcs, k->npcs, (size_t)k->npc_count * sizeof(*k->npcs));
    state->npc_origin_valid = k->npc_origin_valid;
    state->npc_origin_x = k->npc_origin_x;
    state->npc_origin_z = k->npc_origin_z;
    /* Keys sit on tick boundaries, where SERVER_TICK_END has put the cursor
     * back on the root. */
    state->active_root = 1;
}

void
ReplayLedger_EntityAdvance(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState* state,
    int tick)
{
    uint32_t end;

    assert(ledger);
    assert(state);
    assert(tick >= state->tick);
    assert(tick <= ledger->tick_count);
    end = ledger->first_frame[tick];
    while( state->next_frame < end )
        ledger_entity_step(ledger, NULL, state, state->next_frame);
    state->tick = tick;
}

static struct ReplayLedgerPlayerOp const*
player_op_find(
    struct ReplayLedger const* ledger,
    uint64_t ref);

/* The speaker's appearance, natural order, for a chat line: a copy kept in
 * the build's records or a reference into the recording, decoded into one
 * buffer per line. */
static void
chat_appearance(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState const* state,
    struct ReplayChatLine* line,
    uint8_t* buf,
    int cap)
{
    uint64_t const ref = line->appearance_ref;
    struct ReplayLedgerPlayerOp const* copy;

    (void)state;
    if( ref == REPLAY_STICKY_NONE )
        return;
    copy = player_op_find(ledger, ref);
    if( !copy || copy->blob_len <= 0 || copy->blob_len > cap )
        return;
    if( copy->blob_order >= 0 )
        blob_natural(ledger->rec->data + copy->blob_offset, copy->blob_len, copy->blob_order, buf);
    else
        memcpy(buf, ledger->blobs + copy->blob_offset, (size_t)copy->blob_len);
    line->appearance = buf;
    line->appearance_len = copy->blob_len;
}

int
ReplayLedger_EntityStepChat(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState* state,
    struct ReplayChatLine* out,
    int cap)
{
    enum
    {
        APPEARANCE_MAX = 512,
        LINES_MAX = 16
    };
    static uint8_t s_appearance[LINES_MAX][APPEARANCE_MAX];
    int count;

    assert(ledger);
    assert(state);
    assert(out);
    assert(cap > 0);
    assert(state->next_frame < (uint32_t)ledger->rec->frame_count);
    g_chat_out = out;
    g_chat_cap = cap < LINES_MAX ? cap : LINES_MAX;
    g_chat_count = 0;
    ledger_entity_step(ledger, NULL, state, state->next_frame);
    count = g_chat_count;
    g_chat_out = NULL;
    while( state->tick < ledger->tick_count && ledger->first_frame[state->tick + 1] <= state->next_frame )
        state->tick++;
    for( int i = 0; i < count; i++ )
        chat_appearance(ledger, state, &out[i], s_appearance[i], APPEARANCE_MAX);
    return count;
}

uint32_t
ReplayEntityState_NextFrame(struct ReplayEntityState const* state)
{
    assert(state);
    return state->next_frame;
}

int
ReplayEntityState_Tick(struct ReplayEntityState const* state)
{
    assert(state);
    return state->tick;
}

int
ReplayEntityState_NpcSlots(
    struct ReplayEntityState const* state,
    int* slots,
    int cap)
{
    assert(state);
    assert(slots || cap == 0);
    for( int i = 0; i < state->npc_count && i < cap; i++ )
        slots[i] = state->npcs[i].slot;
    return state->npc_count;
}

int
ReplayEntityState_Equal(
    struct ReplayEntityState const* a,
    struct ReplayEntityState const* b,
    char* why,
    int why_cap)
{
    assert(a);
    assert(b);
    assert(why);
    assert(why_cap > 0);
    why[0] = '\0';
    if( a->tick != b->tick || a->next_frame != b->next_frame )
    {
        snprintf(why, (size_t)why_cap, "at tick %d frame %u vs tick %d frame %u", a->tick,
                 a->next_frame, b->tick, b->next_frame);
        return 0;
    }
    if( memcmp(&a->pi, &b->pi, sizeof(a->pi)) != 0 )
    {
        for( int i = 0; i < REPLAY_LEDGER_PLAYER_SLOTS; i++ )
            if( a->pi.high_res[i] != b->pi.high_res[i] || a->pi.coord[i] != b->pi.coord[i] ||
                a->pi.low_res_pos[i] != b->pi.low_res_pos[i] )
            {
                snprintf(why, (size_t)why_cap,
                         "decoder tables differ at slot %d (high %d/%d coord %d/%d)", i,
                         a->pi.high_res[i], b->pi.high_res[i], (int)a->pi.coord[i],
                         (int)b->pi.coord[i]);
                return 0;
            }
        snprintf(why, (size_t)why_cap, "decoder tables differ (flags, speeds or header)");
        return 0;
    }
    if( memcmp(a->player_sticky, b->player_sticky, sizeof(a->player_sticky)) != 0 )
    {
        for( int i = 0; i < REPLAY_LEDGER_PLAYER_SLOTS; i++ )
            for( int s = 0; s < REPLAY_PLAYER_STICKY_COUNT; s++ )
                if( a->player_sticky[i][s] != b->player_sticky[i][s] )
                {
                    snprintf(why, (size_t)why_cap, "player %d sticky %d: %llx vs %llx", i, s,
                             (unsigned long long)a->player_sticky[i][s],
                             (unsigned long long)b->player_sticky[i][s]);
                    return 0;
                }
    }
    if( a->npc_count != b->npc_count )
    {
        snprintf(why, (size_t)why_cap, "npc count %d vs %d", a->npc_count, b->npc_count);
        return 0;
    }
    for( int i = 0; i < a->npc_count; i++ )
        if( memcmp(&a->npcs[i], &b->npcs[i], sizeof(a->npcs[i])) != 0 )
        {
            snprintf(why, (size_t)why_cap,
                     "npc %d of the list: slot %d type %d at %d,%d vs slot %d type %d at %d,%d",
                     i, a->npcs[i].slot, a->npcs[i].type, a->npcs[i].x, a->npcs[i].z,
                     b->npcs[i].slot, b->npcs[i].type, b->npcs[i].x, b->npcs[i].z);
            return 0;
        }
    if( a->npc_origin_valid != b->npc_origin_valid || a->npc_origin_x != b->npc_origin_x ||
        a->npc_origin_z != b->npc_origin_z || a->active_root != b->active_root )
    {
        snprintf(why, (size_t)why_cap, "npc origin or active world differ");
        return 0;
    }
    return 1;
}

/* --- the keyframe's entities ----------------------------------------------- */

static int
cmp_u64(
    void const* pa,
    void const* pb)
{
    uint64_t a = *(uint64_t const*)pa;
    uint64_t b = *(uint64_t const*)pb;

    return a < b ? -1 : a > b ? 1 : 0;
}

static struct ReplayLedgerPlayerOp const*
player_op_find(
    struct ReplayLedger const* ledger,
    uint64_t ref)
{
    int lo = 0;
    int hi = ledger->player_op_count;

    while( lo < hi )
    {
        int mid = lo + (hi - lo) / 2;

        if( ledger->player_ops[mid].ref < ref )
            lo = mid + 1;
        else
            hi = mid;
    }
    if( lo < ledger->player_op_count && ledger->player_ops[lo].ref == ref )
        return &ledger->player_ops[lo];
    return NULL;
}

/* The NPC_INFO frame last decoded for a keyframe: the npc stream is
 * stateless, so a frame decodes the same way whenever it is asked. */
struct NpcDecode
{
    uint32_t frame;
    int count;
    RsprotInfoList list;
};

static int
npc_op_resolve(
    struct ReplayLedger const* ledger,
    struct NpcDecode* cache,
    uint64_t ref,
    struct ReplayWorldKeyframe* kf)
{
    uint32_t f = REPLAY_STICKY_FRAME(ref);
    int index = REPLAY_STICKY_OP(ref);
    RsprotInfoOp const* src;
    RsprotInfoOp* op;

    if( cache->frame != f )
    {
        struct RsproxRecordingFrame const* fr = &ledger->rec->frames[f];
        int name = ledger_frame_name(ledger, f);

        cache->frame = f;
        if( name == PKT_NAME_NPC_INFO_LARGE )
            cache->count = ledger->rev->npc_info_read_large(ledger->rec->data + fr->offset,
                                                            (int)fr->length, &cache->list);
        else
        {
            assert(name == PKT_NAME_NPC_INFO);
            cache->count = ledger->rev->npc_info_read(ledger->rec->data + fr->offset,
                                                      (int)fr->length, &cache->list);
        }
    }
    if( index >= cache->count )
        return 0;
    src = &cache->list.ops[index];
    op = ledger_kf_op(kf);
    *op = *src;
    /* Pointers into the decode arena move into the keyframe's. */
    if( src->kind == RSPROT_OP_NAME_CHANGE )
        op->name_change.name = (char const*)ledger_kf_put(
            kf, src->name_change.name, src->name_change.name ? strlen(src->name_change.name) + 1 : 0);
    else if( src->kind == RSPROT_OP_BODY_CUSTOM )
    {
        RsprotInfoBodyCustom const* b = &src->body;

        op->body.models = (int32_t const*)ledger_kf_put(
            kf, b->models, b->models ? (size_t)b->model_count * sizeof(int32_t) : 0);
        op->body.recolours = (int32_t const*)ledger_kf_put(
            kf, b->recolours, b->recolours ? (size_t)b->recolour_count * sizeof(int32_t) : 0);
        op->body.retextures = (int32_t const*)ledger_kf_put(
            kf, b->retextures, b->retextures ? (size_t)b->retexture_count * sizeof(int32_t) : 0);
        op->body.identkits = (int32_t const*)ledger_kf_put(
            kf, b->identkits, b->identkits ? (size_t)b->identkit_count * sizeof(int32_t) : 0);
    }
    return 1;
}

static int
player_op_resolve(
    struct ReplayLedger const* ledger,
    uint64_t ref,
    struct ReplayWorldKeyframe* kf)
{
    struct ReplayLedgerPlayerOp const* copy = player_op_find(ledger, ref);
    RsprotInfoOp* op;

    if( !copy )
        return 0;
    op = ledger_kf_op(kf);
    *op = copy->op;
    if( copy->op.kind == RSPROT_OP_APPEARANCE && copy->blob_len > 0 )
    {
        /* Into the keyframe's arena, in natural order, as the reader gave it. */
        if( copy->blob_order >= 0 )
        {
            uint8_t* natural = malloc((size_t)copy->blob_len);

            assert(natural);
            blob_natural(ledger->rec->data + copy->blob_offset, copy->blob_len, copy->blob_order,
                         natural);
            op->appearance.appearance =
                (uint8_t const*)ledger_kf_put(kf, natural, (size_t)copy->blob_len);
            free(natural);
        }
        else
            op->appearance.appearance = (uint8_t const*)ledger_kf_put(
                kf, ledger->blobs + copy->blob_offset, (size_t)copy->blob_len);
    }
    return 1;
}

/* One NPC_HEADICON_SPECIFIC sticky slot, parsed again from its frame. */
static int
npc_headicon_resolve(
    struct ReplayLedger const* ledger,
    uint64_t ref,
    int npc_slot,
    struct ReplayWorldKeyframe* kf)
{
    struct RevPacket pkt;
    struct ReplayWorldNpcHeadicon* h;

    if( ref == REPLAY_STICKY_NONE )
        return 1;
    if( !ledger_parse(ledger, REPLAY_STICKY_FRAME(ref), &pkt) )
        return 0;
    assert(pkt._npc_headicon_specific.npc_index == npc_slot);
    kf->npc_headicons = realloc(kf->npc_headicons, (size_t)(kf->npc_headicon_count + 1) *
                                                       sizeof(*kf->npc_headicons));
    assert(kf->npc_headicons);
    h = &kf->npc_headicons[kf->npc_headicon_count++];
    h->npc_slot = npc_slot;
    h->icon_slot = pkt._npc_headicon_specific.slot;
    h->group = pkt._npc_headicon_specific.group;
    h->index = pkt._npc_headicon_specific.index;
    gameproto_free(&pkt);
    return 1;
}

int
ledger_entity_keyframe(
    struct ReplayLedger const* ledger,
    struct ReplayEntityState const* state,
    struct ReplayWorldKeyframe* kf)
{
    struct NpcDecode cache;
    int ok = 1;
    int players = 0;

    assert(ledger);
    assert(state);
    assert(kf);
    memset(&cache, 0, sizeof(cache));
    cache.frame = UINT32_MAX;
    cache.list.ops = malloc(LEDGER_INFO_OPS * sizeof(RsprotInfoOp));
    cache.list.bytes = malloc(LEDGER_INFO_BYTES);
    assert(cache.list.ops);
    assert(cache.list.bytes);
    rsprot_info_list_init(&cache.list, cache.list.ops, LEDGER_INFO_OPS, cache.list.bytes,
                          LEDGER_INFO_BYTES);

    kf->player_table_size = sizeof(state->pi);
    kf->player_table = malloc(sizeof(state->pi));
    assert(kf->player_table);
    memcpy(kf->player_table, &state->pi, sizeof(state->pi));
    kf->local_index = state->pi.local_index;
    kf->npc_origin_valid = state->npc_origin_valid;
    kf->npc_origin_x = state->npc_origin_x;
    kf->npc_origin_z = state->npc_origin_z;

    for( int i = 0; i < REPLAY_LEDGER_PLAYER_SLOTS; i++ )
        players += state->pi.high_res[i] != 0;
    kf->players = calloc((size_t)(players > 0 ? players : 1), sizeof(*kf->players));
    assert(kf->players);
    for( int i = 0; i < REPLAY_LEDGER_PLAYER_SLOTS; i++ )
    {
        struct ReplayWorldPlayer* p;
        uint64_t refs[REPLAY_PLAYER_STICKY_COUNT];
        uint32_t coord;

        if( !state->pi.high_res[i] )
            continue;
        p = &kf->players[kf->player_count++];
        coord = (uint32_t)state->pi.coord[i];
        p->pid = i;
        p->x = REPLAY_TILE_X(coord);
        p->z = REPLAY_TILE_Z(coord);
        p->level = REPLAY_TILE_LEVEL(coord);
        p->op_first = kf->op_count;
        memcpy(refs, state->player_sticky[i], sizeof(refs));
        qsort(refs, REPLAY_PLAYER_STICKY_COUNT, sizeof(refs[0]), cmp_u64);
        for( int s = 0; s < REPLAY_PLAYER_STICKY_COUNT && refs[s] != REPLAY_STICKY_NONE; s++ )
            ok &= player_op_resolve(ledger, refs[s], kf);
        p->op_count = kf->op_count - p->op_first;
    }

    kf->npcs = calloc((size_t)(state->npc_count > 0 ? state->npc_count : 1), sizeof(*kf->npcs));
    assert(kf->npcs);
    for( int i = 0; i < state->npc_count; i++ )
    {
        struct ReplayLedgerNpc const* src = &state->npcs[i];
        struct ReplayWorldNpc* npc = &kf->npcs[kf->npc_count++];
        uint64_t refs[REPLAY_NPC_HEADICON_SPECIFIC];

        npc->slot = src->slot;
        npc->type = src->type;
        npc->spawn_type = src->spawn_type;
        npc->x = src->x;
        npc->z = src->z;
        npc->level = src->level;
        npc->op_first = kf->op_count;
        /* The NPC_INFO ops; the head-icon packets after them, below. */
        memcpy(refs, src->sticky, sizeof(refs));
        qsort(refs, REPLAY_NPC_HEADICON_SPECIFIC, sizeof(refs[0]), cmp_u64);
        for( int s = 0; s < REPLAY_NPC_HEADICON_SPECIFIC && refs[s] != REPLAY_STICKY_NONE; s++ )
            ok &= npc_op_resolve(ledger, &cache, refs[s], kf);
        npc->op_count = kf->op_count - npc->op_first;
        for( int s = 0; s < REPLAY_NPC_HEADICON_SLOTS; s++ )
            ok &= npc_headicon_resolve(
                ledger, src->sticky[REPLAY_NPC_HEADICON_SPECIFIC + s], src->slot, kf);
    }
    ledger_kf_fixup(kf);
    free(cache.list.ops);
    free(cache.list.bytes);
    return ok;
}
